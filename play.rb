#!/usr/bin/env ruby
require './lib/redrat'
require './lib/redrat_metaobject.rb'

class Object
  def banner *args
    puts "\n" * 2, *args
  end

  module Syntax
    def u s
      RedRat::MetaObject::StandardDelegator.new(
        RedRat::Internal::unicode(s))
    end

    class U
      def self.[] s
        u s
      end
      def self.** s
        u s
      end

      def self.* s
        u s
      end

      def self.- s
        u s
      end

    end
  end

  include Syntax

  def pe &block
    begin
      block.call
    rescue RedRat::Internal::RedRatException => e
      builtins = RedRat::MetaObject::StandardDelegator.new RedRat::Internal::builtins
      import = builtins[u'__import__']
      traceback = import.call u'traceback'
      puts "Redrat Reason: " +  e.redrat_reason.to_s
      puts "Python Exception Type: " + e.python_type.to_s
      puts "Python Exception Value: " + e.python_value.to_s
      unless e.python_traceback.nil?
        join_func = (u'\t\n').join
        format_tb = traceback.format_tb
        tb_ary = format_tb.call(e.python_traceback)
        s = "\n" + join_func.call(tb_ary).to_s
        puts "Python Traceback: " + s
      end
    end
  end

  def test_exceptions
    int = builtins[u'int']
    zero = int.call(u'0')

    banner 'Now dividing by zero with a bound method'

    pe {
      zero / zero
    }

    banner 'Now dividing by zero with an unbound method'

    pe {
      int.__div__.call(zero, zero)
    }
  end

  def test_kwargs
    banner 'Now calling with keywords'

    dict = builtins[u'dict']

    puts dict.call { |kw|
      kw[:foo] = u'a value'
    }
  end

  def test_parse_date string
    import = builtins[u'__import__']
    dateutil_parser = (import.call u'dateutil.parser').parser
    puts dateutil_parser.parse(U-string)
  end

  def test_special_operators
    import = builtins[u'__import__']
    int = builtins[u'int']

    a_string = u'a string '

    banner 'String catenation via special operator'

    puts a_string + a_string

    banner 'String multiplication via special operator'

    puts a_string * int.call(u'4')
  end

  def test_dynamic_attribute_binding
    banner 'Dynamic attribute binding'

    textwrap = import.call u'textwrap'
    textwrap.foo = u'from dynamically bound textwrap.foo'
    puts textwrap.foo
  end

  def test_function_application
    import = builtins[u'__import__']
    dict = import.call(u'dict')
    puts builtins[u'eval'].call(
      U-"lambda: 'hello world'", dict.call, dict.call).call
  end

  def builtins
    RedRat::MetaObject::StandardDelegator.new RedRat::Internal::builtins
  end


  pe {
    import = builtins[u'__import__']
    sys = import.call(u'sys')
    exceptions = import.call(u'exceptions')
    argparse = import.call u'argparse'
    list = builtins[u'list']

    sys.argv = (list.call)

    argv = list.call
    ARGV.each { |a| argv.append(U-a) }
    sys.argv = argv

    parser = (argparse.ArgumentParser).call { |kw|
      kw[:description] = u'Play with Redrat.'
    }

    parser.add_argument(u'--exceptions') { |kw|
      kw[:action] = u'store_true'
      kw[:help] = u'trigger some exceptions'
    }

    parser.add_argument(u'--kwargs') { |kw|
      kw[:action] = u'store_true'
      kw[:help] = u'use python keyword arguments'
    }

    parser.add_argument(u'--special-operators') { |kw|
      kw[:action] = u'store_true'
      kw[:help] = u'use special operators like *, +, et al'
    }

    parser.add_argument(u'--function-application') { |kw|
      kw[:action] = u'store_true'
      kw[:help] = u'create a function with eval() and then apply it'
    }

    parser.add_argument(u'--dynamic-attribute-binding') { |kw|
      kw[:action] = u'store_true'
      kw[:help] = u'bind an attribute dynamically to a Python value'
    }

    begin
      parsed = parser.parse_args(argv)

      if parsed.exceptions
        test_exceptions
      end
      if parsed.kwargs
        test_kwargs
      end
      if parsed.special_operators
        test_special_operators
      end
      if parsed.function_application
        test_function_application
      end
      if parsed.dynamic_attribute_binding
        test_dynamic_attribute_binding
      end
    rescue RedRat::Internal::RedRatException => e
      if e.python_type == exceptions.SystemExit
        exit
      else
        raise
      end
    end
  }
end
