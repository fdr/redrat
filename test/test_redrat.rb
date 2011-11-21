require "test/unit"
require "redrat"

class TestRedrat < Test::Unit::TestCase
  def get_builtin name
    RedRat::apply(
      RedRat::getattr(RedRat::builtins, :__getitem__),
      RedRat::unicode(name))
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
    RedRat::apply(int_parse_function, RedRat::unicode('42'))
  end

  def test_exception
    begin
      get_builtin 'really doesn\'t exist'
    rescue RedRat::RedRatException => e
      e.python_exception
      e.redrat_reason
    end
  end

  def test_repr
    str = get_builtin('str')
    p_hi = RedRat::apply(str, RedRat::unicode('hi'))
    if RedRat::repr(p_hi) != '\'hi\''
      raise
    end
  end

  def test_str
    str = get_builtin('str')
    p_hi = RedRat::apply(str, RedRat::unicode('hi'))
    if RedRat::str(p_hi) != 'hi'
      raise
    end
  end

  def test_truth
    int_parse_function = get_builtin('int')
    import = get_builtin '__import__'
    operator = RedRat::apply(import, RedRat::unicode('operator'))

    ops = {}
    [:lt, :le, :eq, :ne, :gt, :ge].each { |sym|
      ops[sym] = RedRat::getattr(operator, sym)
    }

    pv_42 = RedRat::apply(int_parse_function, RedRat::unicode('42'))
    pv_32 = RedRat::apply(int_parse_function, RedRat::unicode('32'))

    if RedRat::truth(RedRat::apply(ops[:lt], pv_42, pv_32))
      raise
    end

    if RedRat::truth(RedRat::apply(ops[:le], pv_42, pv_32))
      raise
    end

    if RedRat::truth(RedRat::apply(ops[:eq], pv_42, pv_32))
      raise
    end

    if RedRat::truth(RedRat::apply(ops[:ne], pv_42, pv_32))
    else
      raise
    end

    if RedRat::truth(RedRat::apply(ops[:gt], pv_42, pv_32))
    else
      raise
    end

    if RedRat::truth(RedRat::apply(ops[:ge], pv_42, pv_32))
    else
      raise
    end
  end
end
