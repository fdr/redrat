require "test/unit"
require "redrat"

class TestRedrat < Test::Unit::TestCase
  def get_builtin name
    RedRat::Internal::apply(
      RedRat::Internal::getattr(
        RedRat::Internal::builtins,
        RedRat::Internal::unicode('__getitem__')),
      RedRat::Internal::unicode(name))
  end

  def test_module_declaration
    RedRat
  end

  def test_builtin_existence
    RedRat::Internal::builtins
  end

  def test_property_access
    get_builtin('str')
  end

  def test_must_reject_no_args_to_apply
    begin
      RedRat::Internal::apply
    rescue ArgumentError
    end
  end

  def test_must_reject_blocks
    begin
      RedRat::Internal::apply 'hello' do |blah|
      end
    rescue ArgumentError
    end
  end

  def test_must_accept_ruby_objects
    begin
      RedRat::Internal::apply 'hello' '1', '2'
    rescue RedRat::Internal::RedRatException => e
      if (RedRat::Internal::str(e.python_value) !=
          "'redrat.RubyObject' object is not callable")
        raise
      end
    end
  end

  def test_function_call_from_string
    int_parse_function = get_builtin('int')
    RedRat::Internal::apply(int_parse_function, RedRat::Internal::unicode('42'))
  end

  def test_exception
    begin
      get_builtin 'really doesn\'t exist'
    rescue RedRat::Internal::RedRatException => e
      e.redrat_reason
      e.python_type
      e.python_value
      e.python_traceback
    end
  end

  def test_repr
    str = get_builtin('str')
    p_hi = RedRat::Internal::apply(str, RedRat::Internal::unicode('hi'))
    if RedRat::Internal::repr(p_hi) != '\'hi\''
      raise
    end
  end

  def test_str
    str = get_builtin('str')
    p_hi = RedRat::Internal::apply(str, RedRat::Internal::unicode('hi'))
    if RedRat::Internal::str(p_hi) != 'hi'
      raise
    end
  end

  def test_truth
    int_parse_function = get_builtin('int')
    import = get_builtin '__import__'
    operator = RedRat::Internal::apply(
      import, RedRat::Internal::unicode('operator'))

    ops = {}
    [:lt, :le, :eq, :ne, :gt, :ge].each { |sym|
      ops[sym] = RedRat::Internal::getattr(
        operator, RedRat::Internal::unicode(sym.to_s))
    }

    pv_42 = RedRat::Internal::apply(
      int_parse_function, RedRat::Internal::unicode('42'))
    pv_32 = RedRat::Internal::apply(
      int_parse_function, RedRat::Internal::unicode('32'))

    if RedRat::Internal::truth(RedRat::Internal::apply(ops[:lt], pv_42, pv_32))
      raise
    end

    if RedRat::Internal::truth(RedRat::Internal::apply(ops[:le], pv_42, pv_32))
      raise
    end

    if RedRat::Internal::truth(RedRat::Internal::apply(ops[:eq], pv_42, pv_32))
      raise
    end

    if RedRat::Internal::truth(RedRat::Internal::apply(ops[:ne], pv_42, pv_32))
    else
      raise
    end

    if RedRat::Internal::truth(RedRat::Internal::apply(ops[:gt], pv_42, pv_32))
    else
      raise
    end

    if RedRat::Internal::truth(RedRat::Internal::apply(ops[:ge], pv_42, pv_32))
    else
      raise
    end
  end
end
