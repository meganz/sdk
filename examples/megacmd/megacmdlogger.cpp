#include "megacmdlogger.h"

#include <map>

using namespace std;

// different outstreams for every thread. to gather all the output data
map<int,ostream *> outstreams; //TODO: put this somewhere inside MegaCmdOutput class
map<int,int> threadLogLevel;

int getCurrentThread(){
    //return std::this_thread::get_id();
    //return std::thread::get_id();
    return MegaThread::currentThreadId();//TODO: create this in thread class
}

ostream &getCurrentOut(){
    int currentThread=getCurrentThread();
    if ( outstreams.find(currentThread) == outstreams.end() ) {
      return cout;
    } else {
      return *outstreams[currentThread];
    }
}

int getCurrentThreadLogLevel(){
    int currentThread=getCurrentThread();
    if ( threadLogLevel.find(currentThread) == threadLogLevel.end() ) {
      return -1;
    } else {
      return threadLogLevel[currentThread];
    }
}

void setCurrentThreadLogLevel(int level){
    threadLogLevel[getCurrentThread()]=level;
}

void setCurrentThreadOutStream(ostream *s){
    outstreams[getCurrentThread()]=s;
}
