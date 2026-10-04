import io, sys
from pathlib import Path
import cv2, numpy as np
REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUN0 = REPO / 'runs/daemon/runs/run0'
from PIL import Image
def load(jpg):
    raw = jpg.read_bytes(); assert raw.endswith(b'\xff\xd9')
    img = Image.open(io.BytesIO(raw)); img.load()
    return np.asarray(img.convert('RGB'), dtype=np.uint8).max(axis=2)
H=W=None
accs=[]
for k in range(1,19):
    ml = load(RUN0/f'cal_E1_L179_{k}.jpg')
    accs.append(ml>=200)
    H,W = ml.shape
yy,xx = np.mgrid[0:H,0:W]
band = (yy>=150)&(yy<=622)
u18 = np.zeros((H,W),bool)
for a in accs: u18 |= (a&band)
print('union18 seeds px:', int(u18.sum()))
u1 = accs[4]&band
print('single k5 seeds px:', int(u1.sum()))
k3 = cv2.getStructuringElement(cv2.MORPH_RECT,(7,7))
k5 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(11,11))
k9 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(19,19))
k9r = cv2.getStructuringElement(cv2.MORPH_RECT,(19,19))
k4 = cv2.getStructuringElement(cv2.MORPH_RECT,(9,9))
for name,m in [('union18 k9e',u18),('single k9e',u1),('union18 k5e',u18),('union18 k4r',u18)]:
    ker = {'union18 k9e':k9,'single k9e':k9,'union18 k5e':k5,'union18 k4r':k4}[name]
    d = cv2.dilate(m.astype(np.uint8),ker)>0
    print(f'{name:14s} dilated px {int(d.sum()):7d}  {int(d.sum())/600:.1f}/site')
d = cv2.dilate(u1.astype(np.uint8),k5)>0
print('single k5 k5e', int(d.sum()), int(d.sum())/600)
d = cv2.dilate((accs[0]&band).astype(np.uint8),k9)>0
print('single k1 k9e', int(d.sum()), int(d.sum())/600)