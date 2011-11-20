= redrat

* https://github.com/fdr/redrat

== DESCRIPTION:

Ruby and Python speak C.

== FEATURES/PROBLEMS:

* Bootstraps low level access to Python constructs to enable Python
  utilization in Ruby.

* Bootstraps low level access to Python constructs to enable Python
  utilization in Ruby.

== SYNOPSIS:

   require 'redrat'

   def get_builtin name
     RedRat::apply(
       RedRat::getattr(RedRat::builtins, :__getitem__),
       RedRat::unicode(name))
   end

   # Only comes with Python in version 2.7
   import = get_builtin '__import__'
   argparse = RedRat::apply(import, RedRat::unicode('argparse'))
   ArgumentParser = RedRat::getattr(argparse, :ArgumentParser)
   RedRat::apply(get_builtin('help'), ArgumentParser)

== REQUIREMENTS:

* python of some version (tested most with 2.7)
* python-config in PATH, or specified to rake compile

== INSTALL:

* gem install redrat

== DEVELOPERS:

Depends on 'hoe'.  I cannot claim to understand it, as I cribbed it
from other sources.  Comments welcome.  One can run the tests with:

  $ rake test

== LICENSE:

(BSD, 3 clause)

Copyright (c) 2011, Daniel Farina
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

Neither the name of the Daniel Farina nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
