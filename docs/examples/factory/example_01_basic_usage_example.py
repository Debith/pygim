# type: ignore

from pygim.gimmicks import EntangledClass


class Factory(EntangledClass):
    """ """
    
    
class GameFactory(Factory):    
    def create_rock(self):
        return "rock"


class GameFactory(Factory):
    def create_scissor(self):
        return "scissor"





print(Factory.__pygim_namespace__)
print(GameFactory.__pygim_namespace__)
print(GameFactory.__pygim_namespace__)

f = GameFactory()
print(f.create_rock())
print(f.create_scissor())