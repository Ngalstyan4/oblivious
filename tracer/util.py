import re
from collections import Counter, OrderedDict
import struct
import os
import pickle
from tqdm import tqdm
import math
from augheapq import AugHeapq

# Return: [(page addr, run-length)...]
def parse_trace(filename):
    lst = list()
    with open(filename, 'rb') as f:
        while (page := f.read(8)):
            page = struct.unpack('q', page)[0]
            lst.append(page)
    return lst

# Return set containing unique page addresses
def get_unique_pages(lst):
    return set(lst)

# Return: [(start, end, region label)....]
def parse_pmap(filename):
    regex = re.compile(r'(\w+)\s+(\d+)K\s+.{5}\s+(.+)')
    with open(filename) as f:
        lst = regex.findall(f.read())
    lst = [(int(x[0], 16), int(x[1], 10), x[2]) for x in lst]
    lst = sorted(lst, key=lambda x: x[0])
    ranges = [(x[0], (x[0] + x[1]*1024 - 1), x[2]) for x in lst]

    # Ensure the pmap ranges don't overlap
    assert all(ranges[idx][1] < ranges[idx+1][0] for idx in range(len(ranges)-1))
    return ranges

# Map a single page to a region
def map_single_page(page, pmap):
    for lower, upper, region in pmap:
        if lower <= page <= upper:
            return region
    return None

# Returns a Counter object: {page, count}
def get_page_count(pages):
    ret = Counter()
    for page, count in pages:
        ret[page] += count
    return ret

# Class to describe cache operations
class Cache_Change:
    def __init__(self, access_num=None, fetches=None):
        self.access_num = access_num
        self.fetches = fetches

    def get_accesses(self):
        return self.fetches

    def __repr__(self):
        return "Access Num: {}, Fetch: {}"\
        .format(self.access_num, self.fetches)

    def __str__(self):
        return self.__repr__()

# Returns a dictionary: {Access_Num: Cache_Change}
def trace_to_mod_list(filename, cache_size, batch_size, beladys=False, pkl=True):
    cache = None
    if beladys:
        cache = AugHeapq()
    else:
        cache = OrderedDict()

    modifications = dict()
    access_num = 0
    fetches = list()
    batch_start = 0

    pkl_path = 'pkls/%s_%s_%s_%s.pkl' % (filename, 'beladys' if beladys else 'lru', cache_size, batch_size)
    if pkl and os.path.isfile(pkl_path):
        f = open(pkl_path, 'rb')
        modifications = pickle.load(f)
        f.close()
        return modifications

    with open(filename, 'rb') as f:
        f.seek(0, os.SEEK_END)
        filesize = f.tell()
        f.seek(0)
        iter_range = range(filesize//16) if beladys else range(filesize // 8)

        for t in tqdm(iter_range, desc='Creating mod_list', miniters=100000):
           # if t % 1000000 == 0:
           #     cache._assert_max_heap()
           #     for (prio, _, _) in cache.heap:
           #         assert(prio >= access_num)
            page = f.read(8)
            page = struct.unpack('q', page)[0]

            next_access = 0
            if beladys:
                next_access = f.read(8)
                next_access = struct.unpack('q', next_access)[0]
                if next_access == -1:
                    next_access =  math.inf

            if page not in cache:
                if len(cache) == cache_size:
                    # Remove an element if the cache is full
                    if beladys:
                        cache.pop()
                    else:
                        cache.popitem(last=False)
                if beladys:
                    cache.push(next_access, page)
                else:
                    cache[page] = 0
                # Handle batching
                if len(fetches) == 0:
                    batch_start = access_num

                fetches.append(page)
                assert(len(fetches) <= batch_size)
            else:
                if beladys:
                    # this ensures accesses are updated even when there is a cache hit
                    # otherwise, if a page is accessed at times 1,3,5,7 times, in even times 4 and 8 the page
                    # would otherwise be evicted since it would initially be inseted with next access time
                    cache.change_priority(page, next_access)
                else:
                    cache.move_to_end(page)

            # Assign entire batch to an access number if appropriate
            if len(fetches) == batch_size:
                modifications[batch_start] = Cache_Change(access_num, list(fetches))
                fetches.clear()

            access_num += 1

    # Handle tail accesses that don't constitute an entire batch
    if fetches:
        modifications[batch_start] = Cache_Change(access_num, list(fetches))
    if pkl:
        os.makedirs(os.path.dirname(pkl_path), exist_ok=True)
        pkl_file = open(pkl_path, 'wb')
        pickle.dump(modifications, pkl_file)
        pkl_file.close()

    print("num modifications:", len(modifications))
    return modifications

