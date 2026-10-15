#!/usr/bin/env python3
import sys, json, base64, io
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def render(latex):
    fig = plt.figure(figsize=(0.01, 0.01))
    fig.text(0, 0, f"${latex}$", fontsize=12)
    buf = io.BytesIO()
    fig.savefig(buf, format='svg', bbox_inches='tight',
                pad_inches=0.05, transparent=True)
    plt.close(fig)
    return buf.getvalue()

def make_image(latex):
    svg = render(latex)
    b64 = base64.b64encode(svg).decode()
    # Image [attr, alt, target]
    # attr   = [id, classes, keyvals]
    # alt    = [] (список inline-элементов)
    # target = [url, title]
    return {'t': 'Image', 'c': [
            ['', ['math'], []],
            [],
            [f'data:image/svg+xml;base64,{b64}', '']
        ]}

def walk(o):
    if isinstance(o, dict):
        if o.get('t') == 'Math':
            latex = o['c'][1]
            try:
                return make_image(latex)
            except Exception as e:
                sys.stderr.write(f"math2svg: cannot render '{latex}': {e}\n")
                return o
        return {k: walk(v) for k, v in o.items()}
    if isinstance(o, list):
        return [walk(x) for x in o]
    return o

json.dump(walk(json.load(sys.stdin)), sys.stdout)
