from pygim.magic import cached_type

def pytest_runtest_call(item):
    cached_type.CachedType.reset_type_cache(type_cache=True, instance_cache=True)
    item.runtest()
    cached_type.CachedType.reset_type_cache(type_cache=True, instance_cache=True)