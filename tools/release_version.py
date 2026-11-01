"""Shared release version/channel rules; no network or credential access."""
import re
from functools import total_ordering

_PATTERN=re.compile(r'v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:\.(0|[1-9][0-9]*))?(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?\Z')

@total_ordering
class Version:
    def __init__(self,text):
        if not isinstance(text,str) or len(text.removeprefix('v'))>23:raise ValueError('Release version must fit 23 characters')
        m=_PATTERN.fullmatch(text)
        if not m:raise ValueError('Use X.Y.Z with an optional SemVer prerelease')
        self.text=text.removeprefix('v')
        self.core=tuple(int(n or 0) for n in m.group(1,2,3))
        if any(n>0xffffffff for n in self.core):raise ValueError('Version component is too large')
        self.pre=tuple((0,int(p)) if p.isdigit() else (1,p) for p in (m.group(4) or '').split('.') if p)
        if m.group(4) and any(p.isdigit() and len(p)>1 and p.startswith('0') for p in m.group(4).split('.')):raise ValueError('Prerelease numbers cannot have leading zeroes')
    @property
    def prerelease(self):return bool(self.pre)
    def _key(self):return self.core,(1,()) if not self.pre else (0,self.pre)
    def __lt__(self,other):return self._key()<other._key()
    def __eq__(self,other):return isinstance(other,Version) and self._key()==other._key()

def select_release(releases,channel):
    """Beta may advance beyond stable; a stable release always outranks its beta."""
    if channel not in ('stable','beta'):raise ValueError('Unknown update channel')
    candidates=[]
    for item in releases:
        if item.get('draft',item.get('isDraft',False)):continue
        tag=item.get('tag_name',item.get('tagName',''))
        try:v=Version(tag)
        except ValueError:continue
        if channel=='stable' and (v.prerelease or item.get('prerelease',item.get('isPrerelease',False))):continue
        candidates.append((v,item))
    return max(candidates,key=lambda pair:pair[0])[1] if candidates else None
