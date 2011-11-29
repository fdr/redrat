module RedRat::MetaObject
  class StandardDelegator
    include RedRat::Internal

    def initialize(python_value)
      # Cannot initialize StandardDelegator with non-PythonValue
      assert { python_value.class == PythonValue }

      @python_value = python_value

      # A horde of convenience variables that unbox useful Python
      # functionality.  Each line is accompanied by the roughly
      # equivalent Python.

      # __builtins__.__dict__.__getitem__
      @@get_builtin = getattr(builtins, unicode('__getitem__'))

      # import
      @@import = apply(@@get_builtin, unicode('__import__'))

      # issubclass
      @@issubclass = apply(@@get_builtin, unicode('issubclass'))

      # setattr
      @@setattr = apply(@@get_builtin, unicode('setattr'))

      # dict
      @@dict = apply(@@get_builtin, unicode('dict'))

      # callable
      @@callable = apply(@@get_builtin, unicode('callable'))

      # import exceptions
      @@py_exceptions_module = apply(@@import, unicode('exceptions'))

      # import operator
      @@py_operator_module = apply(@@import, unicode('operator'))

      # import inspect
      @@py_inspect_module = apply(@@import, unicode('inspect'))

      # protocol: x == y
      @@py_eq_proto = getattr(@@py_operator_module, unicode('eq'))

      # exceptions.AttributeError
      @@attribute_error = getattr(@@py_exceptions_module,
        unicode('AttributeError'))

      # operator.setitem
      @@setitem = getattr(@@py_operator_module, unicode('setitem'))

      # apply (the *actual* Python one from the builtins)
      @@apply = apply(@@get_builtin, unicode('apply'))
    end

    def !
      !truth(@python_value)
    end

    def != other
      !truth(apply(@@py_eq_proto, @python_value,  other))
    end

    def __getobj__
      # Retrieves the underlying object being delegated to.
      #
      # Method name appropriated from the "delegate" Ruby stdlib
      @python_value
    end

    def call *args
      propagate_delegation {
        # Unwrap all delegations to access the underlying
        # PythonValues, including self and all arguments for this
        # .call.
        apply(@python_value, *(args.map { |a| a.__getobj__ }))
      }
    end

    def method_missing(m, *args, &block)
      begin
        propagate_delegation {
          if m == :[]=
              # Specially handle the two-argument assignment used for
              # square-brackets, as it's unlike all other regular
              # assignments.
              if args.length != 2
                raise ArgumentError.new(
                "ArgumentError: wrong number of " +
                "arguments(#{args.length} for 2)")
              end

            apply(@@setitem, @python_value, *args)
          elsif m[-1] == '='
            # Special self[key] = value case must be addressed
            # previously.
            assert { m != :[]= }
            if args.length != 1
              raise ArgumentError.new(
                "ArgumentError: wrong number of " +
                "arguments(#{args.length} for 1)")
            end

            apply(@python_value, unicode(m[0..-2]), args[0].__getobj__)
          elsif args.length > 0
            apply(@@apply, getattr(@python_value, unicode(m.to_s)))
          else
            # Attribute reference case, where no arguments are passed
            assert { args.length == 0 }
            getattr(@python_value, unicode(m.to_s))
          end
        }
      rescue RedRatException => e
        is_attribute_error = propagate_delegation {
          apply(@@issubclass, e.python_type.__getobj__, @@attribute_error)
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
          assert { e.instance_variable_get(pvalsym).class == self}
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

    private

    def assert &block
      # TODO: Provide turning off assertions for production
      if !block.call
        raise "RedRat: Assertion Failure"
      end
    end

    def self.options_hash_to_kwargs hash
      # XXX: A kludge to get over the fact that RedRat doesn't yet
      # push down a RubyValue into Python that can respond properly to
      # the mapping protocol, as to avoid the conversion tarpit.
      #
      # NB: also a bootstrapping hazard; nothing in the body of this
      # function should use any code path in the StandardDelegator
      # that may require a complete definition of this function to
      # make progress, for example, function application with keyword
      # argument dictionaries.
      new_pydict = self.new(@@dict).call
      hash.each_pair { |k, v|
        if k.class != self.class
          converted_k = k
        else
          converted_k = propagate_delegation { unicode(k.to_s) }
        end

        new_pydict.__setitem__(converted_k, v)
      }
    end

    def propagate_delegation &block
      # Given a block, ensure that the return value *or* the
      # up-to-three yielded PythonValues in a RedRatException are
      # convereted into Delegations of this type.  The goal is that
      # all new PythonValues that result from any computation on the
      # current object should be wrapped in the current type's
      # delegator.

      begin
        self.class.new(block.call)
      rescue RedRatException => e
        # Rewrite PythonValues in the Exception instance to be
        # delegate instances as the same kind as this delegate
        # instance.
        [:@python_type, :@python_value, :@python_traceback].each { |pvalsym|
          original = e.instance_variable_get(pvalsym)

          if !original.nil?
            delegated_pval = self.class.new(original)
            e.instance_variable_set(pvalsym, delegated_pval)
          end
        }

        raise
      end
    end

  end
end
