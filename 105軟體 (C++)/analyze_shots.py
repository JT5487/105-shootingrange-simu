import json

with open('shots_data.json') as f:
    shots = json.load(f)

s1 = [s for s in shots if s['shooter_id'] == 1]
if not s1:
    print('No shooter 1 data')
    exit()

xs = [s['x'] for s in s1]
ys = [s['y'] for s in s1]
print('Shooter 1: %d shots' % len(s1))
print('  X avg=%.4f  min=%.4f  max=%.4f' % (sum(xs)/len(xs), min(xs), max(xs)))
print('  Y avg=%.4f  min=%.4f  max=%.4f' % (sum(ys)/len(ys), min(ys), max(ys)))
print('  Median X: %.4f' % sorted(xs)[len(xs)//2])
print('  Median Y: %.4f' % sorted(ys)[len(ys)//2])
