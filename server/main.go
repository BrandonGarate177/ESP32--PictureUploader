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
	fmt.Printf("Upload endpoint: https://image-upload-server-719756097849.us-central1.run.app/upload\n")
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
		http.Error(w, "Failed to get file from form", http.StatusBadRequest)
		return
	}
	defer file.Close()

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
	filename := fmt.Sprintf("%s_%s", timestamp, handler.Filename)

	// Upload to Google Cloud Storage
	ctx := context.Background()
	bucket := storageClient.Bucket(bucketName)
	obj := bucket.Object(filename)

	// Create a writer to the GCS object
	wc := obj.NewWriter(ctx)
	wc.ContentType = handler.Header.Get("Content-Type")
	if wc.ContentType == "" {
		// Set content type based on file extension
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
	}

	// Copy the uploaded file to GCS
	_, err = io.Copy(wc, file)
	if err != nil {
		wc.Close()
		http.Error(w, "Failed to upload file to storage", http.StatusInternalServerError)
		return
	}

	// Close the writer to finalize the upload
	if err := wc.Close(); err != nil {
		http.Error(w, "Failed to finalize upload", http.StatusInternalServerError)
		return
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
