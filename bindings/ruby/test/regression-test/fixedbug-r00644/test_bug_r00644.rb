# Copyright(C) 2007 Brazil
#     All rights reserved.
#     This is free software with ABSOLUTELY NO WARRANTY.
#
# You can redistribute it and/or modify it under the terms of
# the GNU General Public License version 2.
require 'test/unit'
require 'senna'
require 'test_tools'

$KCODE = 'u'

class FixedBugRev00644Test < Test::Unit::TestCase
  def test_many_spaces_snippet
    res = Senna::Snip::open('夢', 300, 1, '(', ')', nil, Senna::ENC_UTF8).
    exec('スペースいっぱい　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　夢いっぱい。')
    assert_equal(['(　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　　夢)い'], res)
  end
end
