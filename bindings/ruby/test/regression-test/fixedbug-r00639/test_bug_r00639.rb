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

class FixedBugRev00308Test < Test::Unit::TestCase
  def test_utf8_normalize_dakuhandaku
    res = Senna::Snip::open('贈呈', 100, 3, '(', ')', nil, Senna::ENC_UTF8).
      exec('ﾓﾘﾀﾎﾟ ﾎﾟｲﾝﾄ 贈呈')
    assert_equal(['ﾓﾘﾀﾎﾟ ﾎﾟｲﾝﾄ( 贈呈)'], res)
  end
end
