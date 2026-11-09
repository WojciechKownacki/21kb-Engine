import kb21
import math
import sys
import threading
assert sys.version_info[:3] == (3,14,10)
assert sys.executable.lower().endswith("kb_editor.exe")
assert len(kb21.schema()) > 20
cube = kb21.create_entity("GeneratedCube", "Cube")
kb21.set_property(cube,"Transform","localPosition.x",1.25)
assert kb21.get_property(cube,"Transform","localPosition.x")==1.25
kb21.undo()
cube=kb21.find_entity("GeneratedCube")
assert kb21.get_property(cube,"Transform","localPosition.x")==0
kb21.redo()
cube=kb21.find_entity("GeneratedCube")
assert kb21.get_property(cube,"Transform","localPosition.x")==1.25
for component,property,value in [("Transform","missing",3),("Transform","worldPosition.x",9),("Transform","localPosition.x",True),("Transform","localPosition.x",float("nan"))]:
    try: kb21.set_property(cube,component,property,value)
    except ValueError: pass
    else: raise AssertionError("invalid property was accepted")
assert kb21.get_property(cube,"Transform","localPosition.x")==1.25
kb21.add_component(cube,"Tags")
temporary=kb21.create_entity("Tymczasowy_Żółć")
kb21.delete_entity(temporary)
try: kb21.find_entity("Tymczasowy_Żółć")
except ValueError: pass
else: raise AssertionError("deleted entity remained")
kb21.undo()
assert kb21.find_entity("Tymczasowy_Żółć")
kb21.redo()
try: kb21.get_property(temporary,"Transform","localPosition.x")
except ValueError: pass
else: raise AssertionError("stale handle was accepted")
errors=[]
def worker():
    try: kb21.find_entity("GeneratedCube")
    except RuntimeError as error: errors.append(str(error))
t=threading.Thread(target=worker);t.start();t.join()
assert len(errors)==1 and "owner-thread" in errors[0]
kb21.save_scene()
kb21.log("embedded-python-author PASS 3.14.10 create property history errors thread save")
