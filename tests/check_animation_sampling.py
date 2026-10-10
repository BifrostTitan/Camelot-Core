"""Regression for NIF Euler composition and its outgoing/incoming Hermite fields."""
import math
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_animations import sample_rotation,sample_values,quat_mul

# Rotate +X around X first, then Y: the result must face -Z.
track={'euler':[{'type':1,'keys':[[0,math.pi/2]]},{'type':1,'keys':[[0,math.pi/2]]},
                {'type':1,'keys':[[0,0]]}],'rotations':[]}
q=sample_rotation(track,0,None)
point=quat_mul(quat_mul(q,np.array([1,0,0,0])),q*np.array([-1,-1,-1,1]))[:3]
assert np.allclose(point,[0,0,-1],atol=1e-6),'Cloak Euler rotations applied in the wrong order'
# With unit outgoing/incoming tangents, Hermite must reproduce a straight ramp.
keys=[[0,0,0,1],[1,1,1,0]]
assert np.allclose(sample_values(keys,2,.25,[0],1),[.25]),'NIF tangent directions were reversed'
print('NIF Euler order and Hermite tangent conventions passed')

# TCB export omits tangent parameters: combat explicitly opts into SLERP fallback.
track={'euler':[], 'rotations':[[0,1,0,0,0],[1,0,0,0,1]], 'rotationType':3}
try:
    sample_rotation(track,.5,None)
    raise AssertionError('Strict importer silently accepted varying TCB quaternion keys')
except ValueError:
    pass
assert np.allclose(sample_rotation(track,.5,None,True),[0,0,2**-.5,2**-.5])
assert np.allclose(sample_rotation(track,0,None,True),[0,0,0,1])
assert np.allclose(sample_rotation(track,1,None,True),[0,0,1,0])
print('Combat quaternion fallback is normalized and preserves source endpoints')
