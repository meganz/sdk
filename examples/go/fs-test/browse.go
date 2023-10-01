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

type MyMegaListener struct {
	mega.SwigDirector_MegaListener
	notified bool
	err      *mega.MegaError
	request  *mega.MegaRequest
	m        sync.Mutex
	cv       *sync.Cond
	cwd      *mega.MegaNode
}

func (l *MyMegaListener) OnRequestFinish(api mega.MegaApi, request mega.MegaRequest, e mega.MegaError) {
	req := request.Copy()
	err := e.Copy()
	l.request = &req
	l.err = &err

	if err.GetErrorCode() != mega.MegaErrorAPI_OK {
		fmt.Printf("INFO: Request finished with error\n")
		return
	}

	l.m.Lock()
	defer l.m.Unlock()

	switch request.GetType() {
	case mega.MegaRequestTYPE_LOGIN:
		fmt.Printf("Fetching nodes. Please wait...\n")
		api.FetchNodes()
	case mega.MegaRequestTYPE_FETCH_NODES:
		fmt.Printf("Account correctly loaded\n")
		node := api.GetRootNode()
		l.cwd = &node
	}

	l.notified = true
	l.cv.Broadcast()
}

func (l *MyMegaListener) OnNodesUpdate(api mega.MegaApi, nodes mega.MegaNodeList) {
	if nodes.Swigcptr() == 0 {
		node := api.GetRootNode()
		l.cwd = &node
	} else {
		fmt.Printf("INFO: Nodes updated\n")
	}
}

func (l *MyMegaListener) GetError() *mega.MegaError {
	return l.err
}

func (l *MyMegaListener) GetRequest() *mega.MegaRequest {
	return l.request
}

func (l *MyMegaListener) Wait() {
	// Wait until notified becomes true
	l.m.Lock()
	defer l.m.Unlock()

	for !l.notified {
		l.cv.Wait()
	}
}

func (l *MyMegaListener) Reset() {
	l.err = nil
	l.request = nil
	l.notified = false
}

func main() {
	myListener := MyMegaListener{}
	myListener.cv = sync.NewCond(&myListener.m)
	listener := mega.NewDirectorMegaListener(&myListener)

	fmt.Println("Hello, World!")
	api := mega.NewMegaApi("ht1gUZLZ", "", "MEGA/SDK fs test")
	api.AddListener(listener)

	user, pass := getAuth()
	api.Login(user, pass)
	defer api.Logout()
	myListener.Wait()

	if (*myListener.GetError()).GetErrorCode() != mega.MegaErrorAPI_OK {
		fmt.Println("Login error")
		return
	}
	myListener.Reset()

	for myListener.cwd == nil {
		// Give the terminal a chance to print the stuff it wants
		time.Sleep(1 * time.Second)
	}

	node := api.GetNodeByPath("/")
	if node.Swigcptr() == 0 {
		fmt.Println("Failed to read node!")
		return
	}

	children := api.GetChildren(node)
	if children.Swigcptr() == 0 {
		fmt.Println("Failed to list files!")
		return
	}

	fmt.Printf("Number of children: %d\n", children.Size())
	for i := 0; i < children.Size(); i++ {
		node := children.Get(i)
		fmt.Printf("%s -> %t\n", node.GetName(), node.IsFile())
	}

	fmt.Println("Done!")
}

func getAuth() (username string, password string) {
	fmt.Print("Enter your username: ")
	fmt.Scan(&username)

	fmt.Print("Enter your password: ")
	fmt.Scan(&password)
	return
}
