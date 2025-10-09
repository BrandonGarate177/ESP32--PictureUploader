package main

import (
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"time"
)

const (
	uploadDir = "uploads"
	port      = ":8081"
)

func main() {
	// Create uploads directory if it doesn't exist
	if err := os.MkdirAll(uploadDir, 0755); err != nil {
		log.Fatalf("Failed to create uploads directory: %v", err)
	}

	// Set up routes
	http.HandleFunc("/upload", uploadHandler)
	http.HandleFunc("/", homeHandler)

	// Start server
	fmt.Printf("Server starting on port %s\n", port)
	fmt.Printf("Upload endpoint: http://localhost%s/upload\n", port)

	// Try to start the server with better error handling
	if err := http.ListenAndServe(port, nil); err != nil {
		if err.Error() == "listen tcp :8081: bind: address already in use" {
			fmt.Printf("Port %s is already in use. Please try a different port or stop the existing server.\n", port)
			fmt.Println("You can change the port by modifying the 'port' constant in main.go")
		} else {
			log.Fatalf("Server failed to start: %v", err)
		}
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
		if ext == allowedExt {
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
	filename := fmt.Sprintf("%s_%s%s", timestamp, handler.Filename, ext)
	filepath := filepath.Join(uploadDir, filename)

	// Create the file on disk
	dst, err := os.Create(filepath)
	if err != nil {
		http.Error(w, "Failed to create file on disk", http.StatusInternalServerError)
		return
	}
	defer dst.Close()

	// Copy the uploaded file to the destination file
	_, err = io.Copy(dst, file)
	if err != nil {
		http.Error(w, "Failed to save file", http.StatusInternalServerError)
		return
	}

	// Return success response
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, `{
		"success": true,
		"message": "File uploaded successfully",
		"filename": "%s",
		"size": %d
	}`, filename, handler.Size)

	log.Printf("File uploaded successfully: %s (size: %d bytes)", filename, handler.Size)
}
