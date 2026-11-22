"""Compile production key/discovery code and validate HA payloads, without a broker."""
from pathlib import Path
import subprocess,json,tempfile
folder=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='technics-keys-') as work:
 binary=Path(work)/'keys'
 subprocess.run(['clang++','-std=c++17','-fsanitize=undefined',str(folder/'test_magnet.cpp'),'-o',str(binary)],check=True)
 rows=[line.split('\t',1) for line in subprocess.check_output([str(binary)],text=True).splitlines()]
 assert len(rows)==6
 names=set();ids=set()
 for topic,raw in rows:
  c=json.loads(raw)
  assert c['unique_id'] not in ids;ids.add(c['unique_id']);names.add(c['name'])
  assert c['device']['name']=='Magnet-Technics'
  assert c['device']['identifiers']==['technics_rs_b205_025445535401']
  assert c['payload_on']=='ON' and c['payload_off']=='OFF'
  assert c['state_topic'].startswith('technics_rs_b205/key/')
  assert c['availability_topic']=='technics_rs_b205/availability'
  assert len(topic.encode())+len(raw.encode())+8<1536
 assert names=={'Play','Pause','Stop','PREV','NEXT','Pilot'}
 print('PASS six contacts and MQTT Discovery payloads; no HA commands sent.')
