# Implementation of an augmented Max-Heap data structure that
# that provides efficient implementation of certain functions
# that arn't traditionally available in a heap data structure.

#                          * * * 
# API: 
# AugHeapq(): Create new Heap object

# heap.push(priority, key): Add key to heap with priority (priority used for Max-heap ordering)
# heap.pop(): Remove key with highest priority, return [priority, index, key] list todo:: maybe should avoid returning index?
# heap.change_priority(key, new_priority): Change priority corresponding to key in heap ordering
# key in heap: check if key is currently in heap
# All operations have O(logN) or better complexity where N is number of keys in the heap

class AugHeapq:

    def __init__(self):
       self.heap = []
       self.dict = dict()


    def __len__(self):
        assert(len(self.heap) == len(self.dict))
        return len(self.heap)

    def __contains__(self, key):
        return key in self.dict 

    def __repr__(self):
        return str(self.heap)
    def __str__(self):
        return self.__repr__()

    def parent(i):
        return (i-1)//2

    def push(self, priority, key):
        # v[1] represents index of object v in heap list
        # this is so that when we get a reference to v
        # from page->v mapping in hash table, we are able
        # to locate the element in heap
        v = [priority, -1, key]
        assert (key not in self.dict)
        self.dict[key] = v
        self.heap.append(v)
        ind = len(self.heap) - 1

        self._move_up(ind)

    def pop(self):
        v = self.heap[0]
        key = v[2]
        del self.dict[key]

        self.heap[0] = self.heap.pop()
        self._move_down(0)
        return v


    def _move_up(self, i):
        v = self.heap[i]

        while i > 0 and v[0] > self.heap[AugHeapq.parent(i)][0]:
            self.heap[i] = self.heap[AugHeapq.parent(i)]
            self.heap[i][1] = i
            i = AugHeapq.parent(i)

        self.heap[i] = v
        self.heap[i][1] = i

    def _move_down(self, i):
        v = self.heap[i]
        while 2*i +2 < len(self.heap):
            child1 = self.heap[2*i+1]
            child2 = self.heap[2*i+2]
            max_child = child1 if child1[0] > child2[0] else child2
            child_ind = max_child[1]
            if max_child[0] > v[0]:
                self.heap[i] = max_child
                self.heap[i][1] = i
            else:
                break
            i = child_ind
        self.heap[i] = v
        self.heap[i][1] = i

    def _assert_max_heap(self):
        for i in range(1, len(self.heap)): 
            assert self.heap[i][0] <= self.heap[(i - 1) // 2][0], (i, self.heap[i], self.heap[(i-1)//2])
            assert self.heap[i][1] == i, (i, self.heap[i], self.heap[(i-1)//2])

    def change_priority(self, key, priority):
        assert(key in self.dict)
        v = self.dict[key]
        old_priority, old_index, _  = v
        v[0] = priority
        if priority > old_priority:
            self._move_up(old_index)
        else:
            self._move_down(old_index)

if __name__ == "__main__":

    cache = AugHeapq()
    cache.push(5, 12)
    cache.push(6, 13)
    cache.push(7, 14)
    cache.push(4,15)
    cache._assert_max_heap()
    cache.heap[0][0] = 1
    cache._move_down(0)
    print(cache)
    [cache._move_down(i) for i in range(len(cache))]
    [cache._move_up(i) for i in range(len(cache))]
    cache._assert_max_heap()
    cache.pop()

    print(cache)
    cache._assert_max_heap()
    cache.change_priority(14, 20)
    cache.change_priority(14, 200)
    

    print(cache)
    cache._assert_max_heap()
#     page,next_access = 0x44,15 #...
#     
#     if page not in cache:
#         cache.push(next_access, page)
#     else:
#         cache.change_priority(page, next_access)
# 
# 
