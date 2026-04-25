import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "f-string" in source or "Clone failed!" in source:
            # Replace single braces with double braces for the bash group command
            new_source = []
            for line in cell.get("source", []):
                if "{ echo 'Clone failed!'; exit 1; }" in line:
                    new_source.append(line.replace("{ echo 'Clone failed!'; exit 1; }", "{{ echo 'Clone failed!'; exit 1; }}"))
                else:
                    new_source.append(line)
            nb["cells"][i]["source"] = new_source

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
