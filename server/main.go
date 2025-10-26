package main

import (
	"context"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"cloud.google.com/go/storage"
	"github.com/joho/godotenv"
)

var (
	port = ":8080" // Cloud Run uses PORT env var, defaulting to 8080
)

var (
	storageClient *storage.Client
	bucketName    string
)

func main() {
	// Load .env file
	if err := godotenv.Load(); err != nil {
		log.Println("No .env file found, using system environment variables")
	}

	// Get port from environment variable (required for Cloud Run)
	if envPort := os.Getenv("PORT"); envPort != "" {
		port = ":" + envPort
	}

	// Get bucket name from environment variable
	bucketName = os.Getenv("STORAGE_BUCKET")
	println("Bucket Name:", bucketName)
	if bucketName == "" {
		log.Fatal("STORAGE_BUCKET environment variable is required")
	}

	// Initialize Google Cloud Storage client
	ctx := context.Background()
	var err error
	storageClient, err = storage.NewClient(ctx)
	if err != nil {
		log.Fatalf("Failed to create storage client: %v", err)
	}
	defer storageClient.Close()

	// Set up routes
	http.HandleFunc("/upload", uploadHandler)
	http.HandleFunc("/", homeHandler)

	// Start server
	fmt.Printf("Server starting on port %s\n", port)
	fmt.Printf("Upload endpoint available at: /upload\n")
	fmt.Printf("Using storage bucket: %s\n", bucketName)

	if err := http.ListenAndServe(port, nil); err != nil {
		log.Fatalf("Server failed to start: %v", err)
	}
}

func homeHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	w.Header().Set("Content-Type", "text/html")
	fmt.Fprintf(w, `
<!DOCTYPE html>
<html>
<head>
    <title>Image Upload Server</title>
</head>
<body>
    <h1>Image Upload Server</h1>
    <p>Upload endpoint: <code>/upload</code></p>
    <p>Method: POST</p>
    <p>Content-Type: multipart/form-data</p>
</body>
</html>
`)
}

func uploadHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// Parse the multipart form with a max memory of 32MB
	err := r.ParseMultipartForm(32 << 20) // 32MB
	if err != nil {
		http.Error(w, "Failed to parse multipart form", http.StatusBadRequest)
		return
	}

	// Get the file from the form
	file, handler, err := r.FormFile("file")
	if err != nil {
		log.Printf("Failed to get file from form: %v", err)
		http.Error(w, "Failed to get file from form", http.StatusBadRequest)
		return
	}
	defer file.Close()

	log.Printf("Received file: %s, Size: %d bytes", handler.Filename, handler.Size)

	// Check if file is an image (basic check by extension)
	ext := filepath.Ext(handler.Filename)
	allowedExts := []string{".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp"}
	isImage := false
	for _, allowedExt := range allowedExts {
		if strings.ToLower(ext) == allowedExt {
			isImage = true
			break
		}
	}

	if !isImage {
		http.Error(w, "File must be an image", http.StatusBadRequest)
		return
	}

	// Generate unique filename with timestamp
	timestamp := time.Now().Format("20060102_150405")
	// Remove extension from original filename and add it back properly
	baseName := strings.TrimSuffix(handler.Filename, ext)
	filename := fmt.Sprintf("%s_%s%s", timestamp, baseName, ext)

	// Read file data to validate JPEG and get proper content type
	fileData, err := io.ReadAll(file)
	if err != nil {
		log.Printf("Failed to read file data: %v", err)
		http.Error(w, "Failed to read file data", http.StatusInternalServerError)
		return
	}

	log.Printf("Read %d bytes from file", len(fileData))

	// Validate JPEG data if it's supposed to be a JPEG
	if strings.ToLower(ext) == ".jpg" || strings.ToLower(ext) == ".jpeg" {
		if len(fileData) < 4 {
			log.Printf("File too small to be valid JPEG: %d bytes", len(fileData))
			http.Error(w, "File too small to be valid JPEG", http.StatusBadRequest)
			return
		}

		// Check JPEG magic bytes
		if fileData[0] != 0xFF || fileData[1] != 0xD8 || fileData[2] != 0xFF {
			log.Printf("Invalid JPEG header. Expected FF D8 FF, got %02X %02X %02X",
				fileData[0], fileData[1], fileData[2])
			http.Error(w, "Invalid JPEG file format", http.StatusBadRequest)
			return
		}

		// Check for JPEG end marker
		if len(fileData) >= 2 {
			if fileData[len(fileData)-2] != 0xFF || fileData[len(fileData)-1] != 0xD9 {
				log.Printf("WARNING: Missing JPEG end marker. Expected FF D9, got %02X %02X",
					fileData[len(fileData)-2], fileData[len(fileData)-1])
			}
		}

		log.Printf("✓ JPEG validation passed - SOI: %02X %02X %02X, EOI: %02X %02X",
			fileData[0], fileData[1], fileData[2],
			fileData[len(fileData)-2], fileData[len(fileData)-1])
	}

	// Upload to Google Cloud Storage
	ctx := context.Background()
	bucket := storageClient.Bucket(bucketName)
	obj := bucket.Object(filename)

	// Create a writer to the GCS object
	wc := obj.NewWriter(ctx)

	// Set content type based on file extension (don't trust multipart headers)
	switch strings.ToLower(ext) {
	case ".jpg", ".jpeg":
		wc.ContentType = "image/jpeg"
	case ".png":
		wc.ContentType = "image/png"
	case ".gif":
		wc.ContentType = "image/gif"
	case ".bmp":
		wc.ContentType = "image/bmp"
	case ".webp":
		wc.ContentType = "image/webp"
	default:
		wc.ContentType = "application/octet-stream"
	}

	log.Printf("Setting GCS Content-Type: %s", wc.ContentType)

	// Write the file data to GCS
	bytesWritten, err := wc.Write(fileData)
	if err != nil {
		wc.Close()
		log.Printf("Failed to write to GCS: %v", err)
		http.Error(w, "Failed to upload file to storage", http.StatusInternalServerError)
		return
	}

	log.Printf("Wrote %d bytes to GCS", bytesWritten)

	// Close the writer to finalize the upload
	if err := wc.Close(); err != nil {
		log.Printf("Failed to close GCS writer: %v", err)
		http.Error(w, "Failed to finalize upload", http.StatusInternalServerError)
		return
	}

	log.Printf("✓ Successfully uploaded %s to GCS bucket %s", filename, bucketName)

	// Make the object publicly readable
	acl := obj.ACL()
	if err := acl.Set(ctx, storage.AllUsers, storage.RoleReader); err != nil {
		log.Printf("Warning: Failed to set public access for %s: %v", filename, err)
	}

	// Return success response
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, `{
		"success": true,
		"message": "File uploaded successfully to cloud storage",
		"filename": "%s",
		"bucket": "%s",
		"size": %d
	}`, filename, bucketName, handler.Size)

	log.Printf("File uploaded successfully to GCS: %s/%s (size: %d bytes)", bucketName, filename, handler.Size)
}
