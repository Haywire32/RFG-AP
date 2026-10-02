"""Stable cache identities and a seed-size-independent salvage budget."""
BASE_ID = 867524000
SALVAGE_CACHES = {"Small Cache (50 Salvage)": (BASE_ID+1100, 50),
                  "Medium Cache (200 Salvage)": (BASE_ID+1101, 200),
                  "Large Cache (400 Salvage)": (BASE_ID+1102, 400)}
for size_index, size in enumerate(("Small", "Medium", "Large")):
    for amount in range(10, 2001, 10):
        SALVAGE_CACHES.setdefault(f"{size} Cache ({amount} Salvage)",
                                 (BASE_ID+20000+size_index*2000+amount//10, amount))

def scaled_caches(count):
    if count <= 0:
        return {}
    counts = (count-count*4//10-count//10, count*4//10, count//10)
    base = (50, 200, 400)
    total = sum(n*a for n,a in zip(counts,base))
    amounts = [max(10, min(2000, ((a*24000 + total*5)//(total*10))*10)) for a in base]
    # Nearby ten-salvage steps keep the total close even in large seeds.
    from itertools import product
    candidates = [range(max(10,a-30),min(2000,a+30)+1,10) for a in amounts]
    amounts = min((v for v in product(*candidates) if v[0]<v[1]<v[2]),
                  key=lambda v:(abs(sum(n*a for n,a in zip(counts,v))-24000),
                                sum(abs(a*total-b*24000)/b for a,b in zip(v,base))))
    return {f"{size} Cache ({amount} Salvage)": n
            for size,amount,n in zip(("Small","Medium","Large"),amounts,counts) if n}
