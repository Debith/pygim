"""
"""

from pygim.serialization import dumps


@serializable
@dataclass
class DeepTest:
    value: int = 0


@serializable
@dataclass
class Test:
    another: DeepTest


t = Test(DeepTest(100))
print(serialize([t], how="json"))
print(pickle.dumps([t]))