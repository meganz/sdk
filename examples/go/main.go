package main

// To build:
// ./autogen.sh && ./configure --disable-silent-rules --enable-go --disable-examples && make -j16
// cd examples/go
// ./prep.sh
// LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./libs go run -x ./main.go

// TODO: ./configure --enable-static results in sqlite3 issues with libmega.a?

import (
	"fmt"
	"time"

	mega "example_project/mega"
)

type MyMegaListener struct {
	mega.SwigDirector_MegaListener
}

func (l *MyMegaListener) OnRequestFinish(api mega.MegaApi, request mega.MegaRequest, e mega.MegaError) {
	fmt.Printf("Request finished (%v); Result: %v\n", request.ToString(), e.ToString())

	// TODO: Mutex lock this for return values
	switch request.GetType() {
	case mega.MegaRequestTYPE_LOGIN:
		api.FetchNodes()
	case mega.MegaRequestTYPE_FETCH_NODES:
		api.GetRootNode()
	case mega.MegaRequestTYPE_ACCOUNT_DETAILS:
		account_details := request.GetMegaAccountDetails()
		fmt.Printf("Account details received\n")
		fmt.Printf("Storage: %v of %v (%v %%)\n",
			account_details.GetStorageUsed(),
			account_details.GetStorageMax(),
			100*account_details.GetStorageUsed()/account_details.GetStorageMax())
		fmt.Printf("Pro level: %v\n", account_details.GetProLevel())
	}
}

func (l *MyMegaListener) OnRequestStart(api mega.MegaApi, request mega.MegaRequest) {
	fmt.Printf("Request start: (%v)\n", request.ToString())
}

func main() {
	listener := mega.NewDirectorMegaListener(&MyMegaListener{})

	fmt.Println("Hello, World!")
	api := mega.NewMegaApi("ox8xnQZL")
	api.AddListener(listener)

	user, pass := getAuth()
	api.Login(user, pass)
	defer api.Logout()

	time.Sleep(5 * time.Second)
	fmt.Println("Email: " + api.GetMyEmail())
	api.GetAccountDetails()
	time.Sleep(5 * time.Second)
}

func getAuth() (username string, password string) {
	fmt.Print("Enter your username: ")
	fmt.Scan(&username)

	fmt.Print("Enter your password: ")
	fmt.Scan(&password)
	return
}
