# Thanks to the pg gem for inspiration on how to deal with programs
# that package the ever-useful 'config' binary, such as pg_config or
# python{VERSION}-config.
require 'mkmf'

if ENV['MAINTAINER_MODE']
    $stderr.puts "Maintainer mode enabled."
    $CFLAGS << ' -Wall' << ' -ggdb' << ' -DDEBUG'
end

pyconfig = with_config('python-config') || find_executable('python-config')

if pyconfig
  $stderr.puts "Using config values from %s" % [ pyconfig ]
  $CPPFLAGS << ' ' + `"#{pyconfig}" --includes`.chomp
  $LDFLAGS << ' ' + `"#{pyconfig}" --libs`.chomp
else
  abort "No python-config binary, aborting"
end

find_header('Python.h') or
  abort "Can't find Python.h as indicated by #{pyconfig}"

have_func 'Py_Initialize'

dir_config("redrat_ext")
create_makefile( "redrat_ext" )
