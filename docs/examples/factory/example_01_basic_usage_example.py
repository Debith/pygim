# type: ignore

from pygim.patterns.factory import Factory

MainFactory = Factory[__name__]

assert MainFactory.__pygim_namespace__ == __name__

class RockFactory(MainFactory):    
    def create_rock(self):
        return "rock"
    

@MainFactory.register
class Scissor:
    pass


@MainFactory.register
def create_paper():
    return "paper"


print(Factory.__pygim_namespace__)
print(GameFactory.__pygim_namespace__)
print(GameFactory.__pygim_namespace__)

f = RockFactory()
print(f.create_rock())
print(f.create_scissor())