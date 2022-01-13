# frozen_string_literal: true

class MyEqual3Test < MrubycTestCase

  description "Integer Integer"
  def fixnum_fixnum_case
    assert_equal true,  1 === 1
    assert_equal false, 1 === 2
  end

  description "Integer Float"
  def fixnum_float_case
    assert_equal true,  1 === 1.0
    assert_equal false, 1 === 1.1
  end

  description "Float Integer"
  def float_fixnum_case
    assert_equal true,  1.0 === 1
    assert_equal false, 1.0 === 2
  end

  description "nil"
  def nil_case
    assert_equal true,  nil === nil
    assert_equal false, nil === 1
    assert_equal false, 1 === nil
  end

  description "False, True"
  def boolean_case
    assert_equal true,  false === false
    assert_equal false, false === true
    assert_equal false, true === false
    assert_equal true,  true === true
    assert_equal false, false === 1
    assert_equal false, true === 1
  end

  description "Symbol"
  def symbol_case
    assert_equal true,  :Symbol === :Symbol
    assert_equal false, :Symbol === :Symbol2
  end

  description "String"
  def string_case
    assert_equal true, "abcde" === "abcde"
    assert_equal false, "abcde" === "abcd"
    assert_equal false, "abcd" === "abcde"
    assert_equal false, "abcde" === "abCde"
    assert_equal false, "abcde" === "abcdE"
  end

  description "Array"
  def array_case
    assert_equal true,  [1,2,3] === [1,2,3]

    assert_equal true,  Array === [1,2,3]
    assert_equal false, Array === {}
  end

  description "Hash"
  def hash_case
    assert_equal true,  {:k=>"v"} === {:k=>"v"}
    assert_equal true,  Hash === {:k=>"v"}
    assert_equal false, Hash === [1,2,3]
  end

  description "Range"
  def range_case
    assert_equal true,  Range === (1..3)
    assert_equal true,  (1..3) === 1
    assert_equal true,  (1..3) === 3
    assert_equal false, (1..3) === 4

    assert_equal true,  (1...3) === 1
    assert_equal false, (1...3) === 3
    assert_equal false, (1...3) === 4
  end

  description "Object"
  def object_case
    assert_equal true,  Object === nil
    assert_equal true,  Object === false
    assert_equal true,  Object === true
    assert_equal true,  Object === 1
    assert_equal true,  Object === 1.1
    assert_equal true,  Object === :symbol
    assert_equal true,  Object === Object
    assert_equal true,  Object === []
    assert_equal true,  Object === "ABC"
    assert_equal true,  Object === {}
    assert_equal true,  Object === (1..3)

    assert_equal true,  Object === MyEqual3
    assert_equal true,  Object === MyEqual3.new
  end
end
