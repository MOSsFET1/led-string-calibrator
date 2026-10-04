import io
from pathlib import Path
import cv2, numpy as np
from PIL import Image
REPO = Path('/home/nellie/projects/led-display/poc_survey')
RUN0 = REPO / 'runs/daemon/runs/run0'
def load(jpg):
    raw = jpg.read_bytes(); assert raw.endswith(b'\xff\xd9')
    img = Image.open(io.BytesIO(raw)); img.load()
    return np.asarray(img.convert('RGB'), dtype=np.uint8).max(axis=2)
accs=[]; H=W=None
for k in range(1,19):
    ml = load(RUN0/f'cal_E1_L179_{k}.jpg'); accs.append(ml); H,W = ml.shape
yy,xx = np.mgrid[0:H,0:W]
band = (yy>=150)&(yy<=622)
u18 = np.zeros((H,W),bool)
for a in accs: u18 |= ((a>=200)&band)
kerns = {
 'r4e(9x9)': cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(9,9)),
 'r4r(9x9)': cv2.getStructuringElement(cv2.MORPH_RECT,(9,9)),
 'r5r(11x11)': cv2.getStructuringElement(cv2.MORPH_RECT,(11,11)),
 'r6e(13x13)': cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(13,13)),
 'r3.5c(7x7)r': cv2.getStructuringElement(cv2.MORPH_RECT,(7,7)),
 'r7e(15x15)': cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(15,15)),
 'r8e(17x17)': cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(17,17)),
}
for thr in [200, 210, 220, 230]:
    seeds = u18 if thr==200 else None
    if thr!=200:
        s = np.zeros((H,W),bool)
        for a in accs: s |= ((a>=thr)&band)
        seeds = s
    for kn,k in kerns.items():
        d = cv2.dilate(seeds.astype(np.uint8),k)>0
        print(f'thr{thr} seeds{int(seeds.sum()):6d} {kn:14s} {int(d.sum()):7d} {int(d.sum())/600:.1f}/site')