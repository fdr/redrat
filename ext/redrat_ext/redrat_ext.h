#ifndef REDRAT_EXT_H
#define REDRAT_EXT_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#ifdef RUBY_EXTCONF_H
#include RUBY_EXTCONF_H
#endif

#define bool char
#define true 1
#define false 0

#include "Python.h"
#include "ruby.h"

#endif /* REDRAT_EXT_H*/
