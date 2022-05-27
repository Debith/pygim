"""
This module implements class that collects all registered types.
"""

class CollectingTypeMeta(type):
    def __new__(mcls, name: Text, bases=(), attrs=None, *, cache_class=True, cache_instance=True):
        try:
            if not cache_class:
                raise KeyError("pop!")
            return mcls.__class_cache[name]
        except KeyError:
            cls = mcls.make_class(name, bases, attrs or {})
            mcls.__class_cache[name] = cls
            mcls.__instance_cache_active[cls] = cache_instance
            return cls



class CollectingType(metaclass=CollectingTypeMeta):
    pass