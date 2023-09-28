package main

import (
	"fmt"
	mega "megasdk/megasdk"
)

func main() {
	// Your code goes here
	fmt.Println("Hello, World!")
	a := mega.NewMegaApi("ox8xnQZL", nil, nil, "Go CRUD example")
	user, pass := getAuth()
	a.Login(user, pass)
	fmt.Println("Email: %s", a.GetMyEmail())
}

func getAuth() (username string, password string) {
	fmt.Print("Enter your username: ")
	fmt.Scan(&username)

	fmt.Print("Enter your password: ")
	fmt.Scan(&password)
	return
}
