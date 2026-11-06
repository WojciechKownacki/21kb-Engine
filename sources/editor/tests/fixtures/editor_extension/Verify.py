import kb21
cube=kb21.find_entity("ExtensionCube")
assert kb21.get_property(cube,"Transform","localPosition.x")==1.5
kb21.log("extension-authoring-reload PASS")
