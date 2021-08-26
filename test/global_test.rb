# frozen_string_literal: true

class GlobalTest < MrubycTestCase

  description "RUBY_ENGINE"
  def ruby_engine
    assert_equal "mruby/c", RUBY_ENGINE
  end
end
