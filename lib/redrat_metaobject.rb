module RedRat::MetaObject
  class Shortcuts
    include RedRat::Internal

    attr_reader :get_builtin, :pyapply, :import, :issubclass,
                :setattr, :type, :dict, :list, :py_exceptions_module,
                :py_operator_module, :py_inspect_module,
                :attribute_error, :getitem, :setitem, :eq, :pow,
                :invert, :add, :sub, :pos, :neg, :mul, :div, :mod,
                :rshift, :lshift, :and_, :xor, :or_, :le, :lt, :gt,
                :ge, :cmp, :contains

    def initialize
      # A horde of convenience variables that unbox useful Python
      # functionality.  Each line is accompanied by the roughly
      # equivalent Python.

      # __builtins__.__dict__.__getitem__
      @get_builtin = getattr(builtins, unicode('__getitem__'))

      # apply (the builtin that supports keyword arguments, not the
      # redrat one; note that you cannot bootstrap in this way because
      # without RedRat's apply it's not possible to ask the builtin
      # apply to be applied to any arguments).
      @pyapply = apply(@get_builtin, unicode('apply'))

      # import
      @import = apply(@get_builtin, unicode('__import__'))

      # issubclass
      @issubclass = apply(@get_builtin, unicode('issubclass'))

      # setattr
      @setattr = apply(@get_builtin, unicode('setattr'))

      # dict
      @dict = apply(@get_builtin, unicode('dict'))

      # list
      @list = apply(@get_builtin, unicode('list'))

      # import exceptions
      @py_exceptions_module = apply(@import, unicode('exceptions'))

      # import operator
      @py_operator_module = apply(@import, unicode('operator'))

      # exceptions.AttributeError
      @attribute_error = getattr(@py_exceptions_module,
        unicode('AttributeError'))

      # operator.getitem
      @getitem = getattr(@py_operator_module, unicode('getitem'))

      # operator.setitem
      @setitem = getattr(@py_operator_module, unicode('setitem'))

      # A pile of operators to handle the analogous operators with
      # special precedence rules in Ruby, such those associated with
      # arithmetic operators.
      # x == y
      @pow = getattr(@py_operator_module, unicode('pow'))
      @invert = getattr(@py_operator_module, unicode('invert'))
      @add = getattr(@py_operator_module, unicode('add'))
      @sub = getattr(@py_operator_module, unicode('sub'))
      @pos = getattr(@py_operator_module, unicode('pos'))
      @neg = getattr(@py_operator_module, unicode('neg'))
      @mul = getattr(@py_operator_module, unicode('mul'))
      @div = getattr(@py_operator_module, unicode('div'))
      @mod = getattr(@py_operator_module, unicode('mod'))
      @rshift = getattr(@py_operator_module, unicode('rshift'))
      @lshift = getattr(@py_operator_module, unicode('lshift'))
      @and_ = getattr(@py_operator_module, unicode('and_'))
      @xor = getattr(@py_operator_module, unicode('xor'))
      @or_ = getattr(@py_operator_module, unicode('or_'))
      @le = getattr(@py_operator_module, unicode('le'))
      @lt = getattr(@py_operator_module, unicode('lt'))
      @gt = getattr(@py_operator_module, unicode('gt'))
      @ge = getattr(@py_operator_module, unicode('ge'))
      @cmp = apply(@get_builtin, unicode('cmp'))
      @eq = getattr(@py_operator_module, unicode('eq'))
      @contains = getattr(@py_operator_module, unicode('contains'))
    end
  end

  class StandardDelegator
    include RedRat::Internal

    class KwArgs < StandardDelegator
      include RedRat::Internal

      def []=(k, v)
        super(self.class.new(unicode(k.to_s)), v)
      end
    end

    @@shortCutInstance = RedRat::MetaObject::Shortcuts.new

    def initialize(python_value, shortcuts=@@shortCutInstance)
      # Cannot initialize StandardDelegator with non-PythonValue
      self.class.assert { python_value.class == PythonValue }

      @python_value = python_value
      @sc = shortcuts
    end

    def !
      !truth(@python_value)
    end

    def != other
      !truth(apply(@sc.eq, @python_value,  other))
    end

    def __getobj__
      # Retrieves the underlying object being delegated to.
      #
      # Method name appropriated from the "delegate" Ruby stdlib
      @python_value
    end

    def call(*args, &block)
      self.class.propagate_delegation {
        self.class.uninfectious_call(@sc, @python_value, *args, &block)
      }
    end

    def == other
      apply(@sc.eq, @python_value, other.__getobj__)
    end

    def method_missing(m, *args, &block)
      unboxed = self.class.optional_unwrap(*args)

      begin
        self.class.propagate_delegation {
          case m
          when :[]=
            apply(@sc.setitem, @python_value, *unboxed)
          when :[]
            apply(@sc.getitem, @python_value, *unboxed)
          when :**
            apply(@sc.pow, @python_value, *unboxed)
          when :~
            apply(@sc.invert, @python_value, *unboxed)
          when :+
            apply(@sc.add, @python_value, *unboxed)
          when :-
            apply(@sc.sub, @python_value, *unboxed)
          when :+@
            apply(@sc.pos, @python_value, *unboxed)
          when :-@
            apply(@sc.neg, @python_value, *unboxed)
          when :*
            apply(@sc.mul, @python_value, *unboxed)
          when :/
            apply(@sc.div, @python_value, *unboxed)
          when :%
            apply(@sc.mod, @python_value, *unboxed)
          when :>>
            apply(@sc.rshift, @python_value, *unboxed)
          when :<<
            apply(@sc.lshift, @python_value, *unboxed)
          when :&
            apply(@sc.and_, @python_value, *unboxed)
          when :^
            apply(@sc.xor, @python_value, *unboxed)
          when :|
            apply(@sc.or_, @python_value, *unboxed)
          when :<=
            apply(@sc.le, @python_value, *unboxed)
          when :<
            apply(@sc.lt, @python_value, *unboxed)
          when :>
            apply(@sc.gt, @python_value, *unboxed)
          when :>=
            apply(@sc.ge, @python_value, *unboxed)
          when :<=>
            apply(@sc.cmp, @python_value, *unboxed)
          when :===
            apply(@sc.contains, @python_value, *unboxed)
          else
            if m[-1] == '='
              # Asserts this is not a subscript-assignment, which is a
              # special assignment kind that must be taken care of in
              # other code prior to this that prohibits getting here.
              self.class.assert { m != :[]= }
              if args.length != 1
                raise ArgumentError.new(
                  "ArgumentError: wrong number of " +
                  "arguments(#{args.length} for 1)")
              end

              apply(@sc.setattr, @python_value, unicode(m[0..-2]), unboxed[0])
            else

              # "Normal" attribute references and calling
              attr = getattr(@python_value, unicode(m.to_s))

              if args.length > 0
                self.class.uninfectious_call(@sc, attr, *args, &block)
              else
                # Attribute reference (getattr, without calling it)
                # case, where no arguments are passed.
                self.class.assert { args.length == 0 }
                python_unicode = unicode(m.to_s)
                getattr(@python_value, python_unicode)
              end
            end
          end
        }
      rescue RedRatException => e
        is_attribute_error = !!self.class.propagate_delegation {
          apply(@sc.issubclass, e.python_type.__getobj__, @sc.attribute_error)
        }

        if is_attribute_error
          # Convert this kind of Python exception into a
          # NoMethodError.
          #
          # It is necessary to return a value that has NoMethodError
          # as a parent, as very common Ruby protocols employed by
          # common operators (such as :puts) cannot operate normally,
          # as they expect this error to be raised in very common
          # situations (such as :to_ary not being defined).

          raise NoMethodError.new(
            "redrat undefined method `#{m.to_s}' for #{self.class}")
        else
          raise
        end

        [:@python_type, :@python_value, :@python_traceback].each { |pvalsym|
          # Expect rewritten exceptions
          self.class.assert { e.instance_variable_get(pvalsym).class == self}
        }

        raise
      end
    end

    def inspect
      repr(@python_value)
    end

    def to_s
      str(@python_value)
    end

    # Class functions.  These are not placed on the delegate instance
    # as to avoid ambiguity between calling RedRat utility procedures
    # vs. delegating to Python.

    def self.assert &block
      # TODO: Provide turning off assertions for production
      if !block.call
        raise "RedRat: Assertion Failure"
      end
    end

    def self.propagate_delegation &block
      # Given a block, ensure that the return value *or* the
      # up-to-three yielded PythonValues in a RedRatException are
      # converted into Delegations of this type, should they require
      # conversion (RubyObjects returned from Python do not require
      # conversion).  The goal is that all new PythonValues that
      # result from any computation on the current object should be
      # wrapped in the current type's delegator.

      begin
        val = block.call

        if val.kind_of? RedRat::Internal::PythonValue
          new(val)
        else
          val
        end
      rescue RedRatException => e
        # Rewrite PythonValues in the Exception instance to be
        # delegate instances as the same kind as this delegate
        # instance.
        [:@python_type, :@python_value, :@python_traceback].each { |pvalsym|
          original = e.instance_variable_get(pvalsym)

          if !original.nil?
            if original.kind_of? RedRat::Internal::PythonValue
              val = new(original)
            else
              val = original
            end

            e.instance_variable_set(pvalsym, val)
          end
        }

        raise
      end
    end

    def self.optional_unwrap *args
      # Attempt unwrapping a delegation to a PythonValue, but should
      # that fail, pass the RubyObject directly.
      args.map { |a|
        # XXX: is_a? doesn't seem to properly detect KwArg child
        # instances.  Eh?
        if a.respond_to? :__getobj__
          a.__getobj__
        else
          a
        end
      }
    end

    def self.uninfectious_call shortcuts, python_value, *args, &block
      # Call the underlying PythonValue callable, yielding unwrapped
      # -- hence, "uninfected" -- RedRatException and PythonValue
      # return types.  This code should never be called directly
      # outside of the internal mechanism or for debugging.  It's to
      # avoid double-wrapping of propagate_delegation guards.

      # Unwrap all delegations to access the underlying
      # PythonValues, including self and all arguments for this
      # .call.
      if block_given?
        pyargs = new(RedRat::Internal::apply(shortcuts.list))
        args.each { |a| pyargs.append(a) }
        kwargs = KwArgs.new(RedRat::Internal::apply(shortcuts.dict))

        # XXX: block.call here is expectated to mutate the passed
        # kwargs; perhaps a wart, but otherwise blocks would have to
        # look like:
        #
        # { |kw| kw[:foo] = 'bar'; kw }
        #
        # Note the ugly trailing 'kw'.  We eliminate this by throwing
        # away the block return value and relying on side effects, so
        # blocks can look like:
        #
        # { |kw| kw[:foo] = 'bar' }
        #
        # Ideally, one could return a Ruby hash and, without
        # copying/marshalling, refer to that hash directly in
        # Python.  As-is we go halfway and make a Python Dict that
        # fakes enough of the Ruby 'hash' interface that can be
        # delegated to from Ruby since that's working better at the
        # moment.
        block.call kwargs

        puts [self.new(shortcuts.pyapply), self.new(python_value), pyargs, kwargs].inspect

        RedRat::Internal::apply(
          shortcuts.pyapply, python_value, pyargs.__getobj__,
          kwargs.__getobj__)
      else
        RedRat::Internal::apply(python_value, *self.optional_unwrap(*args))
      end
    end

  end
end
