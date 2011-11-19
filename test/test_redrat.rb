require "test/unit"
require "redrat"

class TestRedrat < Test::Unit::TestCase
  def test_module_declaration
    Object::RedRat
  end

  def test_builtin_existence
    Object::RedRat::builtins
  end

  def test_property_access
    Object::RedRat::builtins['str'].join
  end

  def test_must_reject_no_args_to_apply
    begin
      Object::RedRat::apply
    rescue ArgumentError
    end
  end

  def test_must_reject_blocks
    begin
      Object::RedRat::apply 'hello' do |blah|
      end
    rescue ArgumentError
    end
  end

  def test_must_reject_non_python_values_in_first_argument
    begin
      Object::RedRat::apply 'hello' '1', '2'
    rescue ArgumentError
    end
  end

  def test_function_call_from_string
    int_parse_function = Object::RedRat::builtins['int']
    Object::RedRat::apply(int_parse_function, '42')
  end

  def test_truth
    int_parse_function = Object::RedRat::builtins['int']
    pv_42 = Object::RedRat::apply(int_parse_function, '42')
    pv_32 = Object::RedRat::apply(int_parse_function, '32')

    if pv_42 < pv_32
      raise
    end

    if pv_42 <= pv_32
      raise
    end

    if pv_42 == pv_32
      raise
    end

    if pv_42 != pv_32
    else
      raise
    end

    if pv_42 > pv_32
    else
      raise
    end

    if pv_42 >= pv_32
    else
      raise
    end
  end
end
