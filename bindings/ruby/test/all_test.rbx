#!/usr/bin/env ruby
# Copyright(C) 2006 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'

testdir = File.dirname(__FILE__)
$:.unshift(testdir)

ar_flag = (VERSION >= "1.8.3")
ar = Test::Unit::AutoRunner.new(ar_flag)
if !ar.process_args(ARGV)
  ar.to_run.push(testdir)
end
ar.exclude.push(/\b\.svn\b/)

ar.run

