#include <memory>
#include <cstdlib>
#include "type/lpc_array.h"
#include "gc/mark_sweep.h"

static void mark_array(lpc_gc_object_t *obj)
{
    lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(obj);
    
}

void mark_sweep_gc::mark(lpc_gc_object_t *gcobj)
{
    while (gcobj) {
        gcobj->head.marked = true;
        switch ((value_type)gcobj->head.type) {
            case value_type::array_: {
                mark_array(gcobj);
                break;
            }

            case value_type::mappig_: {

                break;
            }

            case value_type::object_: {

                break;
            }

            case value_type::string_: {

                break;
            }

            case value_type::proto_: {

                break;
            }

            case value_type::closure_: {

                break;
            }

            case value_type::function_: {

                break;
            }

            case value_type::buffer_: {

                break;
            }
        }

        gcobj = (lpc_gc_object_t *)gcobj->head.next;
    }
}

void mark_sweep_gc::mark_phase()
{
    mark(root);
}

void mark_sweep_gc::sweep_phase()
{

}

void mark_sweep_gc::collect_leisure_sapce()
{

}

void mark_sweep_gc::collect()
{
    mark_phase();
    sweep_phase();
}

void * mark_sweep_gc::allocate(luint32_t sz)
{
    // TODO
    
    void *p = malloc(sz);
    if (!p) {
        // TODO
    }

    blocks += sz;

    return p;
}

void mark_sweep_gc::link(lpc_gc_object_t *gcobj, value_type type)
{
    gcobj->head.next = this->root;
    gcobj->head.type = (lint8_t)type;
    this->root = gcobj;
    ++total_objects;
}

