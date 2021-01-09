import sys
import re
import json


OBJ_NAME_R = re.compile(r'^==(\w+\.OBJ)==$', re.IGNORECASE)
FUNC_NAME_R = re.compile(r'^(\w+):$')
FUNC_SIG_R = re.compile(r'^((?:[0-9A-F?]{2} )+)$')


def main(path):
    objs = list()
    obj = None

    with open(path, 'rb') as f:
        lines = f.read().replace(b'\r\n', b'\n').split(b'\n')
        i = 0

        while i < len(lines):
            line = lines[i].decode()

            if line == '':
                i += 1
                continue

            m_name = OBJ_NAME_R.match(line)

            if m_name is not None:
                obj = {
                    'name': m_name.group(1),
                    'sig': '',
                    'labels': list()
                }

                i += 1
            else:
                added = False
                while i < len(lines):
                    line = lines[i].decode()

                    m_func_name = FUNC_NAME_R.match(line)

                    if m_func_name is not None and obj is not None:
                        obj['labels'].append({
                            'name': m_func_name.group(1),
                            'offset': len(obj['sig']) // 3  # 'HH '
                        })

                        i += 1
                    else:
                        m_func_sig = FUNC_SIG_R.match(line)

                        if m_func_sig is not None and obj is not None:
                            obj['sig'] += m_func_sig.group(1)
                            i += 1
                        elif obj is not None:
                            objs.append(obj)
                            added = True
                            break

                if not added and obj is not None:
                    objs.append(obj)

    return objs


if __name__ == '__main__':
    obj_list = main(sys.argv[1])

    with open(sys.argv[1] + '.json', 'w') as w:
        json.dump(obj_list, w, indent=4)
