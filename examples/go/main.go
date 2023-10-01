package main

// To build:
// ./autogen.sh && ./configure --disable-silent-rules --enable-go --disable-examples && make -j16
// cd examples/go
// ./prep.sh
// LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./libs go run -x ./main.go

// TODO: ./configure --enable-static results in sqlite3 issues with libmega.a?

import (
	"fmt"
	"sync"
	"time"

	mega "example_project/mega"
)

type MyMegaRequestListener struct {
	mega.SwigDirector_MegaRequestListener
	notified bool
	err      *mega.MegaError
	request  *mega.MegaRequest
	m        sync.Mutex
	cv       *sync.Cond
}

func (l *MyMegaRequestListener) OnRequestFinish(api mega.MegaApi, request mega.MegaRequest, e mega.MegaError) {
	fmt.Printf("Request finished (%v); Result: %v\n", request.ToString(), e.ToString())

	req := request.Copy()
	err := e.Copy()
	l.request = &req
	l.err = &err

	l.m.Lock()
	defer l.m.Unlock()

	// TODO: Mutex lock this for return values
	switch request.GetType() {
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

	l.notified = true
	l.cv.Broadcast()
}

func (l *MyMegaRequestListener) OnRequestStart(api mega.MegaApi, request mega.MegaRequest) {
	fmt.Printf("Request start: (%v)\n", request.ToString())
}

func (l *MyMegaRequestListener) GetError() *mega.MegaError {
	return l.err
}

func (l *MyMegaRequestListener) GetRequest() *mega.MegaRequest {
	return l.request
}

func (l *MyMegaRequestListener) Wait() {
	// Wait until notified becomes true
	l.m.Lock()
	defer l.m.Unlock()

	for !l.notified {
		l.cv.Wait()
	}
}

func (l *MyMegaRequestListener) Reset() {
	l.m.Lock()
	defer l.m.Unlock()

	l.notified = false
}

func main() {
	myListener := MyMegaRequestListener{}
	myListener.cv = sync.NewCond(&myListener.m)
	listener := mega.NewDirectorMegaRequestListener(&myListener)

	fmt.Println("Hello, World!")
	api := mega.NewMegaApi("ox8xnQZL")
	api.AddRequestListener(listener)

	user, pass := getAuth()
	api.Login(user, pass)
	defer api.Logout()
	myListener.Wait()

	if (*myListener.GetError()).GetErrorCode() != mega.MegaErrorAPI_OK {
		fmt.Println("Login error")
		return
	}

	myListener.Reset()
	api.FetchNodes(listener)
	myListener.Wait()

	if (*myListener.GetError()).GetErrorCode() != mega.MegaErrorAPI_OK {
		fmt.Println("Error fetchning nodes")
		return
	}

	fmt.Println("Email: " + api.GetMyEmail())

	myListener.Reset()
	api.GetAccountDetails()
	myListener.Wait()

	// Give the terminal a chance to print the stuff it wants
	time.Sleep(1 * time.Second)

	fmt.Println("Done!")
}

func getAuth() (username string, password string) {
	fmt.Print("Enter your username: ")
	fmt.Scan(&username)

	fmt.Print("Enter your password: ")
	fmt.Scan(&password)
	return
}
