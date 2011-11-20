require "test/unit"
require "redrat"

class TestRedrat < Test::Unit::TestCase
  def get_builtin name
    RedRat::apply(RedRat::builtins.python_message(:__getitem__), name)
  end

  def test_module_declaration
    RedRat
  end

  def test_builtin_existence
    RedRat::builtins
  end

  def test_property_access
    get_builtin('str')
  end

  def test_must_reject_no_args_to_apply
    begin
      RedRat::apply
    rescue ArgumentError
    end
  end

  def test_must_reject_blocks
    begin
      RedRat::apply 'hello' do |blah|
      end
    rescue ArgumentError
    end
  end

  def test_must_reject_non_python_values_in_first_argument
    begin
      RedRat::apply 'hello' '1', '2'
    rescue ArgumentError
    end
  end

  def test_function_call_from_string
    int_parse_function = get_builtin('int')
    RedRat::apply(int_parse_function, '42')
  end

  def test_exception
    begin
      RedRat::apply(
        RedRat::builtins.python_message(:__getitem__), 'really doesnt exist')
    rescue RedRat::RedRatException => e
      e.python_exception
    end
  end

  def test_repr
    str = get_builtin('str')
    p_hi = RedRat::apply(str, 'hi')
    if RedRat::repr(p_hi) != '\'hi\''
      raise
    end
  end

  def test_str
    str = get_builtin('str')
    p_hi = RedRat::apply(str, 'hi')
    if RedRat::str(p_hi) != 'hi'
      raise
    end
  end

  def test_truth
    int_parse_function = get_builtin('int')
    pv_42 = RedRat::apply(int_parse_function, '42')
    pv_32 = RedRat::apply(int_parse_function, '32')

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
