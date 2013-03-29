require 'mkmf'

$CFLAGS << ' -I../../../'
if have_header('senna.h') and find_library('libsenna', 'sen_init', '../../Release', '../../Debug')
  create_makefile('senna_api', '../../../bindings/ruby')
end
