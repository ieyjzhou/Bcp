// Copyright (C) 2000, International Business Machines
// Corporation and others.  All Rights Reserved.
#ifndef _BCP_ERROR_H
#define _BCP_ERROR_H

// This file is fully docified.

#include <cstdio>
#include <cstdlib>

/** 
    Currently there isn't any error handling in BCP. When an object of this
    type is created, the string stored in the character array of the argument
    is printed out and execution is aborted with <code>abort()</code> (thus a
    core dump is created).
*/
    
class BCP_fatal_error{
public:
   /** The constructor prints out the error message, flushes the stdout buffer
       and aborts execution. */
   BCP_fatal_error(const char * error) {
      printf("%s", error);
      fflush(0);
      abort();
   }
   /** The destructor exists only because it must. */
   ~BCP_fatal_error() {}
};

#endif