require 'mkmf'
dir_config("senna", `senna-cfg --prefix`.chomp)
#$LOCAL_LIBS << '  -lsenna -lmecab -lstdc++'
$LOCAL_LIBS << ' ' + `senna-cfg --libs`.chomp
$CFLAGS << ' ' + `senna-cfg --cflags`.chomp
if have_header("senna.h") and have_library("senna", "sen_init")
#  system "swig -ruby senna_api.i"
  create_makefile("senna_api")
end
