"""Assemble the private website handoff; does not upload or send email."""
from pathlib import Path
import hashlib, json, shutil, zipfile, re
import xml.etree.ElementTree as ET
from pypdf import PdfReader

root=Path(__file__).resolve().parents[1]
dest=root/'Releases/HPDG_Beta7_Site_Package_EN'
dest.mkdir(parents=True,exist_ok=True)
installer=root/'Releases/HPDG_VST3_Setup_0.1.0-beta.7.exe'
guide=root/'output/pdf/HPDG_Beta7_Guide_EN.pdf'
def sha(path): return hashlib.file_digest(path.open('rb'),'sha256').hexdigest()
reader=PdfReader(guide)
verification=(root/'docs/beta7/VERIFICATION.md').read_text(encoding='utf-8')
assert 'PENDING_' not in verification, 'Finish verification before packaging'
results={}
for log in ['beta7-tests-primary.xml','beta7-tests-final.xml']:
    for case in ET.parse(root/'Logs'/log).iter('testcase'):
        results[case.attrib['name']]=case.find('failure') is None and case.find('error') is None and case.find('skipped') is None
assert len(results)==10 and all(results.values()), results
assert len(reader.pages)==10
texts=[page.extract_text() for page in reader.pages]
assert all(t.strip() for t in texts)
text='\n\n'.join(texts)
assert '\ufffd' not in text and '\u25a0' not in text
assert not re.search(r'[\u0400-\u04ff]', text), 'English PDF must not contain Cyrillic'
for phrase in ['Soviet','Black Steel','HPDG Link','repository','GitHub','ATL_PASSPORT','TrapAlgebraEngine','other plugins','compatible Hamlo']:
    assert phrase.lower() not in text.lower(), phrase
for term in ['ATLClassic','DarkTrap','919','Update','hello@hamloprod.org','60 / BPM']:
    assert term in text, term
md='# HPDG Beta 7 - guide and changes\n\nText version of the English PDF.\n\n'
for i,t in enumerate(texts,1):
    lines=t.splitlines()
    lines=[s for s in lines if s!='HAMLOPROD  /  HPDG' and 'Windows x64 / VST3' not in s and s.strip()!=f'{i:02}']
    md+=f'## Page {i}\n\n'+'\n'.join(lines)+'\n\n'
(root/'docs/beta7/CHANGELOG_EN.md').write_text(md,encoding='utf-8')
release={
 'version':'0.1.0-beta.7','title':'HPDG 0.1.0 Beta 7',
 'notes':'Distinct ATLClassic and DarkTrap rules; HPDG 808 engine in Sub808; six new ATL/Dark voices (919 samples); Trap Rolls and triplet grid; Basic Pitch key analysis; fixes for 808 crackle and UI-related audio dropouts.',
 'page':'https://hamloprod.org/en/vst',
 'objectKey':'hpdg/'+installer.name,'sha256':sha(installer),'size':installer.stat().st_size}
(root/'docs/beta7/release.json').write_text(json.dumps(release,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
for file in [installer,Path(str(installer)+'.sha256'),guide,root/'docs/BETA-RELEASE.txt',root/'Releases/HPDG_0.1.0-beta.7_samples.sha256.json']:
    shutil.copy2(file,dest/file.name)
for name in ['EMAIL_EN.txt','SITE_AGENT.md','VERIFICATION.md','CHANGELOG_EN.md','release.json']:
    file=root/'docs/beta7'/name
    s=file.read_text(encoding='utf-8-sig')
    assert not re.search(r'[\u0400-\u04ff]',s), name
    shutil.copy2(file,dest/file.name)
for name in ['EMAIL_EN.txt','BETA-RELEASE.txt','CHANGELOG_EN.md','release.json']:
    s=(dest/name).read_text(encoding='utf-8-sig')
    for phrase in ['Soviet','Black Steel','HPDG Link','repository','GitHub','compatible Hamlo','separate HPDG 808']:
        assert phrase.lower() not in s.lower(), (name,phrase)
files=sorted(f for f in dest.iterdir() if f.is_file() and f.name!='SHA256SUMS.txt')
(dest/'SHA256SUMS.txt').write_text(''.join(f'{sha(f)}  {f.name}\n' for f in files),encoding='ascii')
archive=root/'Releases/HPDG_Beta7_Site_Package_EN.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=1) as z:
    for file in sorted(dest.iterdir()):
        if file.is_file(): z.write(file,dest.name+'/'+file.name)
print(f'PDF: {len(texts)} pages; English-only customer text verified')
print(f'Installer: {release["size"]} bytes; SHA256 {release["sha256"]}')
print(archive)
