import kb21
cube=kb21.find_entity("GeneratedCube")
assert kb21.get_property(cube,"Transform","localPosition.x")==1.25
assert any(c["name"]=="Transform" for c in kb21.schema())
kb21.log("embedded-python-reload PASS")
