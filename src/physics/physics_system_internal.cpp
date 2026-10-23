#include "physics_system_internal.h"
#include "phys_assert.h"
#include "phys_util.h"
#include "phys_mem_new.h"

#include <new>
#include <tl/tl_system.h>
#include "phys_constraint_solver_multithreaded.h"
#include <platform/arch_size.h>

// The decompiled free-list walks indexed T_internal_base links; these give the payload whatever the link size.
template <typename T>
static inline T *free_list_data(typename phys_free_list<T>::T_internal_base *node)
{
    return &((typename phys_free_list<T>::T_internal *)node)->m_data;
}

// Retail adds 8 to a body's constraint count per attached constraint here (the decompiled form incremented a link
// pointer to an 8-byte struct). The value is compared with m_stable_min_contact_count, so it is kept, not "fixed".
#define PHYS_PARTITION_CONSTRAINT_WEIGHT 8

static_assert(offsetof(phys_free_list<user_rigid_body>::T_internal, m_data) == offsetof(phys_free_list<rigid_body>::T_internal, m_data));
static_assert(offsetof(rigid_body, m_constraint_count) == BO1_ARCH_SIZE(264, offsetof(rigid_body, m_constraint_count)));
static_assert(offsetof(rigid_body, m_partition_node) == BO1_ARCH_SIZE(296, offsetof(rigid_body, m_partition_node)));
static_assert(offsetof(rb_inplace_partition_node, m_partition_head) == BO1_ARCH_SIZE(40, offsetof(rb_inplace_partition_node, m_partition_head)));
static_assert(offsetof(rb_inplace_partition_node, m_partition_size) == BO1_ARCH_SIZE(52, offsetof(rb_inplace_partition_node, m_partition_size)));
static_assert(offsetof(phys_free_list<rigid_body_constraint_contact>::T_internal, m_data) == BO1_ARCH_SIZE(8, offsetof(phys_free_list<rigid_body_constraint_contact>::T_internal, m_data)));
static_assert(offsetof(phys_free_list<rigid_body_constraint_custom_orientation>::T_internal, m_data) == BO1_ARCH_SIZE(8, offsetof(phys_free_list<rigid_body_constraint_custom_orientation>::T_internal, m_data)));
static_assert(offsetof(rigid_body_constraint_wheel, m_wheel_flags) == BO1_ARCH_SIZE(176, offsetof(rigid_body_constraint_wheel, m_wheel_flags)));

physics_system *g_physics_system;

void __cdecl rbint::take_last_step(user_rigid_body *rb)
{
    const phys_mat44 *m_dictator; // eax

    if ( (rb->m_flags & 0x20) == 0
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 75,
                 "rb->is_user_rigid_body()",
                 "") )
    {
        __debugbreak();
    }
    if ( !rb->m_dictator
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 76,
                 "rb->m_dictator",
                 "") )
    {
        __debugbreak();
    }
    m_dictator = rb->m_dictator;
    rb->m_mat.x.x = m_dictator->x.x;
    rb->m_mat.x.y = m_dictator->x.y;
    rb->m_mat.x.z = m_dictator->x.z;
    rb->m_mat.y.x = m_dictator->y.x;
    rb->m_mat.y.y = m_dictator->y.y;
    rb->m_mat.y.z = m_dictator->y.z;
    rb->m_mat.z.x = m_dictator->z.x;
    rb->m_mat.z.y = m_dictator->z.y;
    rb->m_mat.z.z = m_dictator->z.z;
    rb->m_mat.w.x = m_dictator->w.x;
    rb->m_mat.w.y = m_dictator->w.y;
    rb->m_mat.w.z = m_dictator->w.z;
}

void __cdecl IPN_merge(rigid_body *dest, rigid_body *source)
{
    rigid_body *m_next_node; // eax

    if ( dest->m_partition_node.m_partition_head != dest
        && _tlAssert("source/physics_system_internal.cpp", 186, "GIPN(dest)->m_partition_head == dest", "") )
    {
        __debugbreak();
    }
    if ( source->m_partition_node.m_partition_head != source
        && _tlAssert(
                 "source/physics_system_internal.cpp",
                 187,
                 "GIPN(source)->m_partition_head == source",
                 "") )
    {
        __debugbreak();
    }
    if ( dest == source && _tlAssert("source/physics_system_internal.cpp", 188, "dest != source", "") )
        __debugbreak();
    dest->m_partition_node.m_partition_tail->m_partition_node.m_next_node = source;
    dest->m_partition_node.m_partition_tail = source->m_partition_node.m_partition_tail;
    dest->m_partition_node.m_partition_size += source->m_partition_node.m_partition_size;
    m_next_node = source;
    do
    {
        m_next_node->m_partition_node.m_partition_head = dest->m_partition_node.m_partition_head;
        m_next_node = m_next_node->m_partition_node.m_next_node;
    }
    while ( m_next_node );
    source->m_partition_node.m_partition_tail = 0;
    source->m_partition_node.m_partition_size = 0;
}

void __cdecl rigid_body_island_qsort(rigid_body **list, int list_count)
{
    rigid_body *v2; // ecx
    rigid_body **v3; // esi
    rigid_body **v4; // edi
    rigid_body **i; // eax
    rigid_body *v6; // ecx
    rigid_body **v7; // ecx
    rigid_body *v8; // esi
    rigid_body **last; // [esp+4h] [ebp-4h]

    if ( list_count >= 3 )
    {
        v3 = list;
        rigid_body_island_qsort(list, list_count / 2);
        rigid_body_island_qsort(&list[list_count / 2], list_count - list_count / 2);
        v4 = &list[list_count / 2];
        last = &list[list_count];
        for ( i = v4 - 1; v4 < last; ++v4 )
        {
            v6 = *i;
            if ( (*v4)->m_partition_node.m_partition_size <= (unsigned int)(*i)->m_partition_node.m_partition_size )
                break;
            *i = *v4;
            *v4 = v6;
            v7 = i - 1;
            if ( i > v3 )
            {
                while ( 1 )
                {
                    v8 = *v7;
                    if ( (unsigned int)(*i)->m_partition_node.m_partition_size <= (unsigned int)(*v7)->m_partition_node.m_partition_size )
                        break;
                    *v7 = *i;
                    *i = v8;
                    v3 = list;
                    i = v7;
                    --v7;
                    if ( i <= list )
                        goto LABEL_12;
                }
                v3 = list;
            }
LABEL_12:
            i = v4;
        }
    }
    else if ( list_count == 2 )
    {
        v2 = *list;
        if ( list[1]->m_partition_node.m_partition_size > (*list)->m_partition_node.m_partition_size )
        {
            *list = list[1];
            list[1] = v2;
        }
    }
}

void __cdecl merge_sort(rigid_body_constraint_contact **list, int list_count)
{
    rigid_body_constraint_contact *v2; // ecx
    rigid_body_constraint_contact **v3; // esi
    rigid_body_constraint_contact **v4; // edi
    rigid_body_constraint_contact **i; // eax
    rigid_body_constraint_contact *v6; // ecx
    rigid_body_constraint_contact **v7; // ecx
    rigid_body_constraint_contact *v8; // esi
    rigid_body_constraint_contact **last; // [esp+4h] [ebp-4h]

    if ( list_count >= 3 )
    {
        v3 = list;
        merge_sort(list, list_count / 2);
        merge_sort(&list[list_count / 2], list_count - list_count / 2);
        v4 = &list[list_count / 2];
        last = &list[list_count];
        for ( i = v4 - 1; v4 < last; ++v4 )
        {
            v6 = *i;
            if ( (*v4)->m_solver_priority >= (*i)->m_solver_priority )
                break;
            *i = *v4;
            *v4 = v6;
            v7 = i - 1;
            if ( i > v3 )
            {
                while ( 1 )
                {
                    v8 = *v7;
                    if ( (*i)->m_solver_priority >= (*v7)->m_solver_priority )
                        break;
                    *v7 = *i;
                    *i = v8;
                    v3 = list;
                    i = v7;
                    --v7;
                    if ( i <= list )
                        goto LABEL_12;
                }
                v3 = list;
            }
LABEL_12:
            i = v4;
        }
    }
    else if ( list_count == 2 )
    {
        v2 = *list;
        if ( list[1]->m_solver_priority < (*list)->m_solver_priority )
        {
            *list = list[1];
            list[1] = v2;
        }
    }
}

void __cdecl IPN_verify_rigid_bodies(rigid_body *rb_partition_head)
{
    rigid_body_constraint_point *i; // esi
    rigid_body_constraint_hinge *j; // esi
    rigid_body *b1; // eax
    unsigned int m_flags; // ecx
    char *p_m_partition_head; // eax
    rigid_body *b2; // eax
    unsigned int v7; // ecx
    char *v8; // eax
    rigid_body_constraint_distance *k; // esi
    rigid_body *v10; // eax
    unsigned int v11; // ecx
    char *v12; // eax
    rigid_body *v13; // eax
    unsigned int v14; // ecx
    char *v15; // eax
    rigid_body_constraint_ragdoll *m; // esi
    rigid_body *v17; // eax
    unsigned int v18; // ecx
    char *v19; // eax
    rigid_body *v20; // eax
    unsigned int v21; // ecx
    char *v22; // eax
    rigid_body_constraint_wheel *n; // esi
    rigid_body *v24; // eax
    unsigned int v25; // ecx
    char *v26; // eax
    rigid_body *v27; // eax
    unsigned int v28; // ecx
    char *v29; // eax
    rigid_body_constraint_angular_actuator *ii; // esi
    rigid_body *v31; // eax
    unsigned int v32; // ecx
    char *v33; // eax
    rigid_body *v34; // eax
    unsigned int v35; // ecx
    char *v36; // eax
    rigid_body_constraint_upright *jj; // esi
    rigid_body *v38; // eax
    unsigned int v39; // ecx
    char *v40; // eax
    rigid_body *v41; // eax
    unsigned int v42; // ecx
    char *v43; // eax
    rigid_body_constraint_custom_orientation *kk; // esi
    rigid_body *v45; // eax
    unsigned int v46; // ecx
    char *v47; // eax
    rigid_body *v48; // eax
    unsigned int v49; // ecx
    char *v50; // eax
    rigid_body_constraint_custom_path *mm; // esi
    rigid_body *v52; // eax
    unsigned int v53; // ecx
    char *v54; // eax
    rigid_body *v55; // eax
    unsigned int v56; // ecx
    char *v57; // eax
    rigid_body_constraint_contact *nn; // esi
    rigid_body *v59; // eax
    unsigned int v60; // ecx
    char *v61; // eax
    rigid_body *v62; // eax
    unsigned int v63; // ecx
    char *v64; // eax

    if ( rb_partition_head->m_partition_node.m_partition_head != rb_partition_head
        && _tlAssert(
                 "source/physics_system_internal.cpp",
                 174,
                 "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                 "") )
    {
        __debugbreak();
    }
    for ( i = rb_partition_head->m_partition_node.m_rbc_point_first; i; i = (rigid_body_constraint_point *)i->m_next )
    {
        g_physics_system->validate_member((environment_rigid_body *)i->b1);
        g_physics_system->validate_member((environment_rigid_body *)i->b2);
    }
    for ( j = rb_partition_head->m_partition_node.m_rbc_hinge_first; j; j = (rigid_body_constraint_hinge *)j->m_next )
    {
        b1 = j->b1;
        if ( j->b1 )
        {
            m_flags = b1->m_flags;
            if ( (m_flags & 0x10) != 0 )
            {
                if ( b1 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                p_m_partition_head = (char *)b1 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (m_flags & 0x20) != 0 )
                    PMM_VALIDATE(p_m_partition_head, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(p_m_partition_head, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        b2 = j->b2;
        if ( b2 )
        {
            v7 = b2->m_flags;
            if ( (v7 & 0x10) != 0 )
            {
                if ( b2 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v8 = (char *)b2 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v7 & 0x20) != 0 )
                    PMM_VALIDATE(v8, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v8, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( k = rb_partition_head->m_partition_node.m_rbc_dist_first; k; k = (rigid_body_constraint_distance *)k->m_next )
    {
        v10 = k->b1;
        if ( k->b1 )
        {
            v11 = v10->m_flags;
            if ( (v11 & 0x10) != 0 )
            {
                if ( v10 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v12 = (char *)v10 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v11 & 0x20) != 0 )
                    PMM_VALIDATE(v12, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v12, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v13 = k->b2;
        if ( v13 )
        {
            v14 = v13->m_flags;
            if ( (v14 & 0x10) != 0 )
            {
                if ( v13 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v15 = (char *)v13 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v14 & 0x20) != 0 )
                    PMM_VALIDATE(v15, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v15, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( m = rb_partition_head->m_partition_node.m_rbc_ragdoll_first; m; m = (rigid_body_constraint_ragdoll *)m->m_next )
    {
        v17 = m->b1;
        if ( m->b1 )
        {
            v18 = v17->m_flags;
            if ( (v18 & 0x10) != 0 )
            {
                if ( v17 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v19 = (char *)v17 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v18 & 0x20) != 0 )
                    PMM_VALIDATE(v19, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v19, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v20 = m->b2;
        if ( v20 )
        {
            v21 = v20->m_flags;
            if ( (v21 & 0x10) != 0 )
            {
                if ( v20 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v22 = (char *)v20 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v21 & 0x20) != 0 )
                    PMM_VALIDATE(v22, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v22, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( n = rb_partition_head->m_partition_node.m_rbc_wheel_first; n; n = (rigid_body_constraint_wheel *)n->m_next )
    {
        v24 = n->b1;
        if ( n->b1 )
        {
            v25 = v24->m_flags;
            if ( (v25 & 0x10) != 0 )
            {
                if ( v24 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v26 = (char *)v24 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v25 & 0x20) != 0 )
                    PMM_VALIDATE(v26, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v26, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v27 = n->b2;
        if ( v27 )
        {
            v28 = v27->m_flags;
            if ( (v28 & 0x10) != 0 )
            {
                if ( v27 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v29 = (char *)v27 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v28 & 0x20) != 0 )
                    PMM_VALIDATE(v29, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v29, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( ii = rb_partition_head->m_partition_node.m_rbc_angular_actuator_first;
                ii;
                ii = (rigid_body_constraint_angular_actuator *)ii->m_next )
    {
        v31 = ii->b1;
        if ( ii->b1 )
        {
            v32 = v31->m_flags;
            if ( (v32 & 0x10) != 0 )
            {
                if ( v31 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v33 = (char *)v31 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v32 & 0x20) != 0 )
                    PMM_VALIDATE(v33, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v33, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v34 = ii->b2;
        if ( v34 )
        {
            v35 = v34->m_flags;
            if ( (v35 & 0x10) != 0 )
            {
                if ( v34 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v36 = (char *)v34 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v35 & 0x20) != 0 )
                    PMM_VALIDATE(v36, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v36, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( jj = rb_partition_head->m_partition_node.m_rbc_upright_first; jj; jj = (rigid_body_constraint_upright *)jj->m_next )
    {
        v38 = jj->b1;
        if ( jj->b1 )
        {
            v39 = v38->m_flags;
            if ( (v39 & 0x10) != 0 )
            {
                if ( v38 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v40 = (char *)v38 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v39 & 0x20) != 0 )
                    PMM_VALIDATE(v40, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v40, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v41 = jj->b2;
        if ( v41 )
        {
            v42 = v41->m_flags;
            if ( (v42 & 0x10) != 0 )
            {
                if ( v41 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v43 = (char *)v41 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v42 & 0x20) != 0 )
                    PMM_VALIDATE(v43, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v43, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( kk = rb_partition_head->m_partition_node.m_rbc_custom_orientation_first;
                kk;
                kk = (rigid_body_constraint_custom_orientation *)kk->m_next )
    {
        v45 = kk->b1;
        if ( kk->b1 )
        {
            v46 = v45->m_flags;
            if ( (v46 & 0x10) != 0 )
            {
                if ( v45 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v47 = (char *)v45 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v46 & 0x20) != 0 )
                    PMM_VALIDATE(v47, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v47, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v48 = kk->b2;
        if ( v48 )
        {
            v49 = v48->m_flags;
            if ( (v49 & 0x10) != 0 )
            {
                if ( v48 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v50 = (char *)v48 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v49 & 0x20) != 0 )
                    PMM_VALIDATE(v50, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v50, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( mm = rb_partition_head->m_partition_node.m_rbc_custom_path_first;
                mm;
                mm = (rigid_body_constraint_custom_path *)mm->m_next )
    {
        v52 = mm->b1;
        if ( mm->b1 )
        {
            v53 = v52->m_flags;
            if ( (v53 & 0x10) != 0 )
            {
                if ( v52 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v54 = (char *)v52 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v53 & 0x20) != 0 )
                    PMM_VALIDATE(v54, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v54, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v55 = mm->b2;
        if ( v55 )
        {
            v56 = v55->m_flags;
            if ( (v56 & 0x10) != 0 )
            {
                if ( v55 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v57 = (char *)v55 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v56 & 0x20) != 0 )
                    PMM_VALIDATE(v57, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v57, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
    for ( nn = rb_partition_head->m_partition_node.m_rbc_contact_first; nn; nn = (rigid_body_constraint_contact *)nn->m_next )
    {
        v59 = nn->b1;
        if ( nn->b1 )
        {
            v60 = v59->m_flags;
            if ( (v60 & 0x10) != 0 )
            {
                if ( v59 != &g_physics_system->m_environment_rigid_body
                    && _tlAssert(
                             "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                             73,
                             "rb == &m_environment_rigid_body",
                             "") )
                {
                    __debugbreak();
                }
            }
            else
            {
                v61 = (char *)v59 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v60 & 0x20) != 0 )
                    PMM_VALIDATE(v61, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v61, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
        v62 = nn->b2;
        if ( v62 )
        {
            v63 = v62->m_flags;
            if ( (v63 & 0x10) != 0 )
            {
                if ( v62 != &g_physics_system->m_environment_rigid_body )
                {
                    if ( _tlAssert(
                                 "C:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\physics_system_internal.h",
                                 73,
                                 "rb == &m_environment_rigid_body",
                                 "") )
                    {
                        __debugbreak();
                    }
                }
            }
            else
            {
                v64 = (char *)v62 - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
                if ( (v63 & 0x20) != 0 )
                    PMM_VALIDATE(v64, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
                else
                    PMM_VALIDATE(v64, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
}

void __thiscall physics_system::generate_partitions_and_stuff(phys_transient_allocator *transient_buffer)
{
    m_environment_rigid_body.m_constraint_count = 0;
    m_environment_rigid_body.m_contact_count = 0;
    m_environment_rigid_body.m_partition_node.m_partition_head = 0;
    for (phys_free_list<user_rigid_body>::T_internal_base *node = m_list_user_rigid_body.m_dummy_head.m_next_T_internal;
        &m_list_user_rigid_body.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        user_rigid_body *urb = free_list_data<user_rigid_body>(node);
        urb->m_constraint_count = 0;
        urb->m_contact_count = 0;
        urb->m_partition_node.m_partition_head = 0;
    }
    for (phys_free_list<rigid_body>::T_internal_base *node = m_list_rigid_body.m_dummy_head.m_next_T_internal;
        &m_list_rigid_body.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body *rb = free_list_data<rigid_body>(node);
        rb->m_constraint_count = 0;
        rb->m_contact_count = 0;
        rb->m_partition_node.m_partition_head = rb;
        rb->m_partition_node.m_partition_tail = rb;
        rb->m_partition_node.m_next_node = 0;
        rb->m_partition_node.m_partition_size = 1;
    }
    m_list_island_count = m_list_rigid_body.m_list_count;
    for (phys_free_list<rigid_body_constraint_point>::T_internal_base *node = m_list_rbc_point.m_dummy_head.m_next_T_internal;
        &m_list_rbc_point.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_point *rbc = free_list_data<rigid_body_constraint_point>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_hinge>::T_internal_base *node = m_list_rbc_hinge.m_dummy_head.m_next_T_internal;
        &m_list_rbc_hinge.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_hinge *rbc = free_list_data<rigid_body_constraint_hinge>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_distance>::T_internal_base *node = m_list_rbc_dist.m_dummy_head.m_next_T_internal;
        &m_list_rbc_dist.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_distance *rbc = free_list_data<rigid_body_constraint_distance>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_ragdoll>::T_internal_base *node = m_list_rbc_ragdoll.m_dummy_head.m_next_T_internal;
        &m_list_rbc_ragdoll.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_ragdoll *rbc = free_list_data<rigid_body_constraint_ragdoll>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_wheel>::T_internal_base *node = m_list_rbc_wheel.m_dummy_head.m_next_T_internal;
        &m_list_rbc_wheel.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_wheel *rbc = free_list_data<rigid_body_constraint_wheel>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if ((rbc->m_wheel_flags & 1) != 0)
        {
            if (rbc->b1)
                ++rbc->b1->m_contact_count;
            if (rbc->b2)
                rbc->b2->m_contact_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        }
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_angular_actuator>::T_internal_base *node = m_list_rbc_angular_actuator.m_dummy_head.m_next_T_internal;
        &m_list_rbc_angular_actuator.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_angular_actuator *rbc = free_list_data<rigid_body_constraint_angular_actuator>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_upright>::T_internal_base *node = m_list_rbc_upright.m_dummy_head.m_next_T_internal;
        &m_list_rbc_upright.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_upright *rbc = free_list_data<rigid_body_constraint_upright>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_custom_orientation>::T_internal_base *node = m_list_rbc_custom_orientation.m_dummy_head.m_next_T_internal;
        &m_list_rbc_custom_orientation.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_custom_orientation *rbc = free_list_data<rigid_body_constraint_custom_orientation>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_custom_path>::T_internal_base *node = m_list_rbc_custom_path.m_dummy_head.m_next_T_internal;
        &m_list_rbc_custom_path.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_custom_path *rbc = free_list_data<rigid_body_constraint_custom_path>(node);
        if (rbc->b1)
            rbc->b1->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        if (rbc->b2)
            rbc->b2->m_constraint_count += PHYS_PARTITION_CONSTRAINT_WEIGHT;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body_constraint_contact>::T_internal_base *node = m_list_rbc_contact.m_dummy_head.m_next_T_internal;
        &m_list_rbc_contact.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_contact *rbc = free_list_data<rigid_body_constraint_contact>(node);
        int count1 = 0;
        for (contact_point_info *cpi = rbc->m_list_contact_point_info_buffer_1.m_first; cpi; cpi = cpi->m_next_link)
            count1 += cpi->m_point_pair_count;
        int count2 = 0;
        for (contact_point_info *cpi = rbc->m_list_contact_point_info_buffer_2.m_first; cpi; cpi = cpi->m_next_link)
            count2 += cpi->m_point_pair_count;
        if (count2 <= count1)
            count2 = count1;
        if (rbc->b1)
            rbc->b1->m_contact_count += count2;
        if (rbc->b2)
            rbc->b2->m_contact_count += count2;
        IPN_partition_process(rbc, &m_list_island_count);
    }
    for (phys_free_list<rigid_body>::T_internal_base *node = m_list_rigid_body.m_dummy_head.m_next_T_internal;
        &m_list_rigid_body.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body *rb = free_list_data<rigid_body>(node);
        if (rb->m_partition_node.m_partition_size > 0)
        {
            if (rb->m_partition_node.m_partition_head != rb
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    141,
                    "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                    ""))
            {
                __debugbreak();
            }
            rb->m_partition_node.m_rbc_point_first = 0;
            rb->m_partition_node.m_rbc_hinge_first = 0;
            rb->m_partition_node.m_rbc_dist_first = 0;
            rb->m_partition_node.m_rbc_ragdoll_first = 0;
            rb->m_partition_node.m_rbc_wheel_first = 0;
            rb->m_partition_node.m_rbc_angular_actuator_first = 0;
            rb->m_partition_node.m_rbc_upright_first = 0;
            rb->m_partition_node.m_rbc_custom_orientation_first = 0;
            rb->m_partition_node.m_rbc_custom_path_first = 0;
            rb->m_partition_node.m_rbc_contact_first = 0;
        }
    }
    for (phys_free_list<rigid_body_constraint_point>::T_internal_base *node = m_list_rbc_point.m_dummy_head.m_next_T_internal;
        &m_list_rbc_point.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_point *rbc = free_list_data<rigid_body_constraint_point>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_point_first;
        head->m_partition_node.m_rbc_point_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_hinge>::T_internal_base *node = m_list_rbc_hinge.m_dummy_head.m_next_T_internal;
        &m_list_rbc_hinge.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_hinge *rbc = free_list_data<rigid_body_constraint_hinge>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_hinge_first;
        head->m_partition_node.m_rbc_hinge_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_distance>::T_internal_base *node = m_list_rbc_dist.m_dummy_head.m_next_T_internal;
        &m_list_rbc_dist.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_distance *rbc = free_list_data<rigid_body_constraint_distance>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_dist_first;
        head->m_partition_node.m_rbc_dist_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_ragdoll>::T_internal_base *node = m_list_rbc_ragdoll.m_dummy_head.m_next_T_internal;
        &m_list_rbc_ragdoll.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_ragdoll *rbc = free_list_data<rigid_body_constraint_ragdoll>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_ragdoll_first;
        head->m_partition_node.m_rbc_ragdoll_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_wheel>::T_internal_base *node = m_list_rbc_wheel.m_dummy_head.m_next_T_internal;
        &m_list_rbc_wheel.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_wheel *rbc = free_list_data<rigid_body_constraint_wheel>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_wheel_first;
        head->m_partition_node.m_rbc_wheel_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_angular_actuator>::T_internal_base *node = m_list_rbc_angular_actuator.m_dummy_head.m_next_T_internal;
        &m_list_rbc_angular_actuator.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_angular_actuator *rbc = free_list_data<rigid_body_constraint_angular_actuator>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_angular_actuator_first;
        head->m_partition_node.m_rbc_angular_actuator_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_upright>::T_internal_base *node = m_list_rbc_upright.m_dummy_head.m_next_T_internal;
        &m_list_rbc_upright.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_upright *rbc = free_list_data<rigid_body_constraint_upright>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_upright_first;
        head->m_partition_node.m_rbc_upright_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_custom_orientation>::T_internal_base *node = m_list_rbc_custom_orientation.m_dummy_head.m_next_T_internal;
        &m_list_rbc_custom_orientation.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_custom_orientation *rbc = free_list_data<rigid_body_constraint_custom_orientation>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_custom_orientation_first;
        head->m_partition_node.m_rbc_custom_orientation_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_custom_path>::T_internal_base *node = m_list_rbc_custom_path.m_dummy_head.m_next_T_internal;
        &m_list_rbc_custom_path.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_custom_path *rbc = free_list_data<rigid_body_constraint_custom_path>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_custom_path_first;
        head->m_partition_node.m_rbc_custom_path_first = rbc;
    }
    for (phys_free_list<rigid_body_constraint_contact>::T_internal_base *node = m_list_rbc_contact.m_dummy_head.m_next_T_internal;
        &m_list_rbc_contact.m_dummy_head != node;
        node = node->m_next_T_internal)
    {
        rigid_body_constraint_contact *rbc = free_list_data<rigid_body_constraint_contact>(node);
        rigid_body *head;
        if (rbc->b1 && rbc->b1->m_partition_node.m_partition_head)
        {
            head = rbc->b1->m_partition_node.m_partition_head;
        }
        else
        {
            if ((!rbc->b2 || !rbc->b2->m_partition_node.m_partition_head)
                && _tlAssert(
                    "source/physics_system_internal.cpp",
                    229,
                    "rbc.get_b2() && GIPN(rbc.get_b2())->m_partition_head",
                    ""))
            {
                __debugbreak();
            }
            head = rbc->b2->m_partition_node.m_partition_head;
        }
        if (head->m_partition_node.m_partition_head != head
            && _tlAssert(
                "source/physics_system_internal.cpp",
                163,
                "GIPN(rb_partition_head)->m_partition_head == rb_partition_head",
                ""))
        {
            __debugbreak();
        }
        rbc->m_next = head->m_partition_node.m_rbc_contact_first;
        head->m_partition_node.m_rbc_contact_first = rbc;
    }
    if (transient_buffer->m_cur
        && _tlAssert("source/physics_system_internal.cpp", 300, "transient_buffer->is_empty()", ""))
    {
        __debugbreak();
    }
    if (m_list_island_count <= 0)
    {
        m_list_island = 0;
    }
    else
    {
        //m_list_island = (rigid_body **)phys_transient_allocator::allocate(
        m_list_island = (rigid_body **)transient_buffer->allocate(
            sizeof(rigid_body *) * m_list_island_count,
            alignof(rigid_body *),
            0,
            "phys_transient_allocator out of memory.");
        int list_island_cur = 0;
        for (phys_free_list<rigid_body>::T_internal_base *node = m_list_rigid_body.m_dummy_head.m_next_T_internal;
            &m_list_rigid_body.m_dummy_head != node;
            node = node->m_next_T_internal)
        {
            rigid_body *rb = free_list_data<rigid_body>(node);
            if (rb->m_partition_node.m_partition_size > 0)
            {
                IPN_verify_rigid_bodies(rb);
                if (list_island_cur >= m_list_island_count
                    && _tlAssert(
                        "source/physics_system_internal.cpp",
                        314,
                        "list_island_cur < m_list_island_count",
                        ""))
                {
                    __debugbreak();
                }
                m_list_island[list_island_cur++] = rb;
            }
        }
        if (list_island_cur != m_list_island_count)
        {
            if (_tlAssert(
                "source/physics_system_internal.cpp",
                319,
                "list_island_cur == m_list_island_count",
                ""))
            {
                __debugbreak();
            }
        }
        rigid_body_island_qsort(m_list_island, m_list_island_count);
    }
}

void __thiscall physics_system::time_step(float outside_delta_t, bool last_step)
{
    phys_free_list<rigid_body>::T_internal_base *m_next_T_internal; // edi
    phys_free_list<rigid_body> *i; // ebx
    phys_free_list<user_rigid_body>::T_internal_base *j; // edi
    void (*m_collision_callback)(void); // eax
    phys_free_list<rigid_body_constraint_contact>::T_internal_base *v8; // ebx
    contact_point_info *m_prev_T_internal; // eax
    phys_free_list<rigid_body_constraint_contact>::T_internal_base *v10; // edx
    int v11; // ecx
    phys_free_list<rigid_body_constraint_contact>::T_internal *v12; // edi
    phys_free_list<rigid_body_constraint_contact>::T_internal_base *v13; // ecx
    phys_free_list<rigid_body_constraint_contact>::T_internal_base *v14; // eax
    phys_free_list<user_rigid_body>::T_internal_base *v15; // edi
    phys_free_list<user_rigid_body> *p_m_list_user_rigid_body; // ebx
    int m_total_memory_allocated; // eax
    phys_transient_allocator::block_header *m_first_block; // eax
    char *m_cur; // ecx
    char *m_end; // edx
    void *m_slot_pool; // ebx
    unsigned int temp_buffer_12; // [esp+20h] [ebp-30h]
    volatile unsigned int temp_buffer_16; // [esp+24h] [ebp-2Ch]
    void *temp_buffer_20; // [esp+28h] [ebp-28h]
    int v26; // [esp+4Ch] [ebp-4h]
    int savedregs; // [esp+50h] [ebp+0h] BYREF

    m_next_T_internal = this->m_list_rigid_body.m_dummy_head.m_next_T_internal;
    this->m_flags |= 1u;
    this->m_outside_sub_delta_t = outside_delta_t;
    for ( i = &this->m_list_rigid_body;
                i != (phys_free_list<rigid_body> *)m_next_T_internal;
                m_next_T_internal = m_next_T_internal->m_next_T_internal )
    {
        rbint::collision_prolog(phys_free_list<rigid_body>::data_of(m_next_T_internal), outside_delta_t);
    }
    for ( j = this->m_list_user_rigid_body.m_dummy_head.m_next_T_internal;
                &this->m_list_user_rigid_body != (phys_free_list<user_rigid_body> *)j;
                j = j->m_next_T_internal )
    {
        rbint::collision_prolog(phys_free_list<user_rigid_body>::data_of(j), outside_delta_t);
    }
    m_collision_callback = this->m_collision_callback;
    if ( m_collision_callback )
        m_collision_callback();
    v8 = this->m_list_rbc_contact.m_dummy_head.m_next_T_internal;
    this->m_flags &= ~1u;
    while ( &this->m_list_rbc_contact != (phys_free_list<rigid_body_constraint_contact> *)v8 )
    {
        m_prev_T_internal = free_list_data<rigid_body_constraint_contact>(v8)->m_list_contact_point_info_buffer_1.m_first;
        v10 = v8;
        v11 = 0;
        if ( !m_prev_T_internal )
            goto LABEL_11;
        do
        {
            v11 += m_prev_T_internal->m_point_pair_count;
            m_prev_T_internal = m_prev_T_internal->m_next_link;
        }
        while ( m_prev_T_internal );
        if ( v11 )
        {
            v8 = v8->m_next_T_internal;
        }
        else
        {
LABEL_11:
            v8 = v8->m_next_T_internal;
            v12 = (phys_free_list<rigid_body_constraint_contact>::T_internal *)v10;
            PMM_VALIDATE((char *)v12, sizeof(*v12), phys_free_list<rigid_body_constraint_contact>::T_internal_alignment());
            if ( !v12
                && _tlAssert("c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\phys_mem.h", 477, "data", "") )
            {
                __debugbreak();
            }
            --this->m_list_rbc_contact.m_list_count;
            //phys_free_list<rigid_body_constraint_contact>::debug_remove(&this->m_list_rbc_contact, v12);
            this->m_list_rbc_contact.debug_remove(v12);
            v13 = v12->m_prev_T_internal;
            v14 = v12->m_next_T_internal;
            v13->m_next_T_internal = v14;
            v14->m_prev_T_internal = v13;
            //rigid_body_constraint_contact::~rigid_body_constraint_contact(&v12->m_data);
            v12->m_data.~rigid_body_constraint_contact();
            PMM_FREE((unsigned __int8 *)v12, sizeof(*v12), phys_free_list<rigid_body_constraint_contact>::T_internal_alignment());
        }
    }
    phys_transient_allocator transient_buffer; // [esp+2Ch] [ebp-24h] BYREF

    v26 = 0;
    physics_system::solver_priority_sort(&transient_buffer);
    physics_system::generate_partitions_and_stuff(&transient_buffer);
    if ( tlScratchpadLocked
        && _tlAssert(
                 "C:\\projects_pc\\cod\\codsrc\\tl\\base\\include\\tl_system.h",
                 215,
                 "!tlScratchpadLocked",
                 "Scratchpad is already locked!") )
    {
        __debugbreak();
    }
    tlScratchpadLocked = 1;
    constraint_solver_process(&transient_buffer, this, outside_delta_t);
    if ( !tlScratchpadLocked
        && _tlAssert(
                 "C:\\projects_pc\\cod\\codsrc\\tl\\base\\include\\tl_system.h",
                 221,
                 "tlScratchpadLocked",
                 "Scratchpad is already unlocked!") )
    {
        __debugbreak();
    }
    tlScratchpadLocked = 0;
    //phys_transient_allocator::reset(&transient_buffer);
    transient_buffer.reset();
    this->m_list_island = 0;
    this->m_list_island_count = 0;
    v15 = this->m_list_user_rigid_body.m_dummy_head.m_next_T_internal;
    p_m_list_user_rigid_body = &this->m_list_user_rigid_body;
    if ( last_step )
    {
        for ( ; p_m_list_user_rigid_body != (phys_free_list<user_rigid_body> *)v15; v15 = v15->m_next_T_internal )
            rbint::take_last_step(phys_free_list<user_rigid_body>::data_of(v15));
    }
    else
    {
        for ( ; p_m_list_user_rigid_body != (phys_free_list<user_rigid_body> *)v15; v15 = v15->m_next_T_internal )
            rbint::take_next_step(phys_free_list<user_rigid_body>::data_of(v15), outside_delta_t);
    }
    m_total_memory_allocated = this->m_contact_point_buffer_1.m_total_memory_allocated;
    if ( this->m_contact_point_buffer_high_water > m_total_memory_allocated )
        m_total_memory_allocated = this->m_contact_point_buffer_high_water;
    this->m_contact_point_buffer_high_water = m_total_memory_allocated;
    //phys_transient_allocator::reset(&this->m_contact_point_buffer_2);
    this->m_contact_point_buffer_2.reset();
    m_first_block = this->m_contact_point_buffer_2.m_first_block;
    m_cur = this->m_contact_point_buffer_2.m_cur;
    m_end = this->m_contact_point_buffer_2.m_end;
    temp_buffer_12 = this->m_contact_point_buffer_2.m_total_memory_allocated;
    temp_buffer_16 = this->m_contact_point_buffer_2.m_mutex.m_count;
    temp_buffer_20 = this->m_contact_point_buffer_2.m_slot_pool;
    this->m_contact_point_buffer_2.m_first_block = this->m_contact_point_buffer_1.m_first_block;
    this->m_contact_point_buffer_2.m_cur = this->m_contact_point_buffer_1.m_cur;
    this->m_contact_point_buffer_2.m_end = this->m_contact_point_buffer_1.m_end;
    this->m_contact_point_buffer_2.m_total_memory_allocated = this->m_contact_point_buffer_1.m_total_memory_allocated;
    this->m_contact_point_buffer_2.m_mutex.m_count = this->m_contact_point_buffer_1.m_mutex.m_count;
    m_slot_pool = this->m_contact_point_buffer_1.m_slot_pool;
    this->m_contact_point_buffer_1.m_first_block = m_first_block;
    this->m_contact_point_buffer_1.m_cur = m_cur;
    this->m_contact_point_buffer_1.m_end = m_end;
    this->m_contact_point_buffer_1.m_total_memory_allocated = temp_buffer_12;
    this->m_contact_point_buffer_1.m_mutex.m_count = temp_buffer_16;
    this->m_contact_point_buffer_2.m_slot_pool = m_slot_pool;
    this->m_contact_point_buffer_1.m_slot_pool = temp_buffer_20;
    if ( m_first_block
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\phys_transient_allocator.h",
                 69,
                 "m_first_block == NULL",
                 "") )
    {
        __debugbreak();
    }
    v26 = -1;
    if ( transient_buffer.m_first_block )
    {
        if ( _tlAssert(
                     "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\phys_transient_allocator.h",
                     69,
                     "m_first_block == NULL",
                     "") )
        {
            __debugbreak();
        }
    }
}

void __thiscall physics_system::frame_advance(float delta_t)
{
    phys_free_list<user_rigid_body>::T_internal_base *i; // edi
    phys_free_list<rigid_body_constraint_point>::T_internal_base *j; // eax
    phys_free_list<rigid_body_constraint_hinge>::T_internal_base *k; // eax
    phys_free_list<rigid_body_constraint_distance>::T_internal_base *m; // edi
    phys_free_list<rigid_body_constraint_ragdoll>::T_internal_base *n; // eax
    phys_free_list<rigid_body_constraint_wheel>::T_internal_base *ii; // eax
    phys_free_list<rigid_body_constraint_angular_actuator>::T_internal_base *jj; // edi
    phys_free_list<rigid_body_constraint_upright>::T_internal_base *kk; // eax
    phys_free_list<rigid_body_constraint_custom_orientation>::T_internal_base *mm; // eax
    phys_free_list<rigid_body_constraint_custom_path>::T_internal_base *nn; // eax
    phys_free_list<rigid_body_constraint_contact>::T_internal_base *m_next_T_internal; // eax
    phys_free_list<rigid_body_constraint_contact> *i1; // edx
    int v15; // eax
    int v16; // edi
    phys_free_list<rigid_body_constraint_point>::T_internal_base *i2; // eax
    phys_free_list<rigid_body_constraint_hinge>::T_internal_base *i3; // eax
    phys_free_list<rigid_body_constraint_distance>::T_internal_base *v19; // edi
    phys_free_list<rigid_body_constraint_ragdoll>::T_internal_base *i4; // eax
    phys_free_list<rigid_body_constraint_wheel>::T_internal_base *i5; // eax
    phys_free_list<rigid_body_constraint_angular_actuator>::T_internal_base *v22; // edi
    phys_free_list<rigid_body_constraint_upright>::T_internal_base *i6; // eax
    phys_free_list<rigid_body_constraint_custom_orientation>::T_internal_base *i7; // eax
    phys_free_list<rigid_body_constraint_custom_path>::T_internal_base *i8; // eax
    phys_free_list<rigid_body_constraint_contact>::T_internal_base *i9; // esi
    float v27; // [esp+14h] [ebp-Ch]
    float outside_delta_t; // [esp+14h] [ebp-Ch]
    float v29; // [esp+18h] [ebp-8h]
    float v30; // [esp+18h] [ebp-8h]
    int sub_count; // [esp+1Ch] [ebp-4h]
    int vars0; // [esp+20h] [ebp+0h] BYREF

    phys_assert_info_frame_advance_all();
    sub_count = (int)(delta_t / this->m_max_delta_t);
    v29 = (double)sub_count * this->m_max_delta_t;
    if (v29 < (double)delta_t)
        ++sub_count;
    v30 = (float)sub_count;
    v27 = v30 * this->m_max_delta_t;
    if (v27 < (double)delta_t
        && _tlAssert("source/physics_system_internal.cpp", 367, "delta_t <= m_max_delta_t * sub_count", ""))
    {
        __debugbreak();
    }
    for (i = this->m_list_user_rigid_body.m_dummy_head.m_next_T_internal;
        &this->m_list_user_rigid_body != (phys_free_list<user_rigid_body> *)i;
        i = i->m_next_T_internal)
    {
        rbint::prolog_frame_advance(phys_free_list<user_rigid_body>::data_of(i), delta_t);
    }
    for (j = this->m_list_rbc_point.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_point != (phys_free_list<rigid_body_constraint_point> *)j;
        j = j->m_next_T_internal)
    {
        ;
    }
    for (k = this->m_list_rbc_hinge.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_hinge != (phys_free_list<rigid_body_constraint_hinge> *)k;
        k = k->m_next_T_internal)
    {
        ;
    }
    for (m = this->m_list_rbc_dist.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_dist != (phys_free_list<rigid_body_constraint_distance> *)m;
        m = m->m_next_T_internal)
    {
        //rigid_body_constraint_distance::outer_prolog_update((rigid_body_constraint_distance *)&m[2], delta_t);
        (phys_free_list<rigid_body_constraint_distance>::data_of(m))->outer_prolog_update(delta_t);
    }
    for (n = this->m_list_rbc_ragdoll.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_ragdoll != (phys_free_list<rigid_body_constraint_ragdoll> *)n;
        n = n->m_next_T_internal)
    {
        ;
    }
    for (ii = this->m_list_rbc_wheel.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_wheel != (phys_free_list<rigid_body_constraint_wheel> *)ii;
        ii = ii->m_next_T_internal)
    {
        ;
    }
    for (jj = this->m_list_rbc_angular_actuator.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_angular_actuator != (phys_free_list<rigid_body_constraint_angular_actuator> *)jj;
        jj = jj->m_next_T_internal)
    {
        //rigid_body_constraint_angular_actuator::outer_prolog_update(
        //    (rigid_body_constraint_angular_actuator *)&jj[2],
        //    COERCE_FLOAT(&vars0),
        //    delta_t);
        (phys_free_list<rigid_body_constraint_angular_actuator>::data_of(jj))->outer_prolog_update(delta_t);
    }
    for (kk = this->m_list_rbc_upright.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_upright != (phys_free_list<rigid_body_constraint_upright> *)kk;
        kk = kk->m_next_T_internal)
    {
        ;
    }
    for (mm = this->m_list_rbc_custom_orientation.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_custom_orientation != (phys_free_list<rigid_body_constraint_custom_orientation> *)mm;
        mm = mm->m_next_T_internal)
    {
        ;
    }
    for (nn = this->m_list_rbc_custom_path.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_custom_path != (phys_free_list<rigid_body_constraint_custom_path> *)nn;
        nn = nn->m_next_T_internal)
    {
        ;
    }
    m_next_T_internal = this->m_list_rbc_contact.m_dummy_head.m_next_T_internal;
    for (i1 = &this->m_list_rbc_contact;
        i1 != (phys_free_list<rigid_body_constraint_contact> *)m_next_T_internal;
        m_next_T_internal = m_next_T_internal->m_next_T_internal)
    {
        ;
    }
    v15 = 0;
    if (sub_count > 0)
    {
        do
        {
            v16 = v15 + 1;
            outside_delta_t = delta_t / v30;
            physics_system::time_step(outside_delta_t, v15 + 1 == sub_count);
            v15 = v16;
        } while (v16 < sub_count);
        i1 = &this->m_list_rbc_contact;
    }
    for (i2 = this->m_list_rbc_point.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_point != (phys_free_list<rigid_body_constraint_point> *)i2;
        i2 = i2->m_next_T_internal)
    {
        ;
    }
    for (i3 = this->m_list_rbc_hinge.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_hinge != (phys_free_list<rigid_body_constraint_hinge> *)i3;
        i3 = i3->m_next_T_internal)
    {
        ;
    }
    v19 = this->m_list_rbc_dist.m_dummy_head.m_next_T_internal;
    if (&this->m_list_rbc_dist != (phys_free_list<rigid_body_constraint_distance> *)v19)
    {
        do
        {
            //rigid_body_constraint_distance::outer_epilog_update((rigid_body_constraint_distance *)&v19[2], delta_t);
            (phys_free_list<rigid_body_constraint_distance>::data_of(v19))->outer_epilog_update(delta_t);
            v19 = v19->m_next_T_internal;
        } while (&this->m_list_rbc_dist != (phys_free_list<rigid_body_constraint_distance> *)v19);
        i1 = &this->m_list_rbc_contact;
    }
    for (i4 = this->m_list_rbc_ragdoll.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_ragdoll != (phys_free_list<rigid_body_constraint_ragdoll> *)i4;
        i4 = i4->m_next_T_internal)
    {
        ;
    }
    for (i5 = this->m_list_rbc_wheel.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_wheel != (phys_free_list<rigid_body_constraint_wheel> *)i5;
        i5 = i5->m_next_T_internal)
    {
        ;
    }
    v22 = this->m_list_rbc_angular_actuator.m_dummy_head.m_next_T_internal;
    if (&this->m_list_rbc_angular_actuator != (phys_free_list<rigid_body_constraint_angular_actuator> *)v22)
    {
        do
        {
            //rigid_body_constraint_angular_actuator::outer_epilog_update((rigid_body_constraint_angular_actuator *)&v22[2], delta_t);
            (phys_free_list<rigid_body_constraint_angular_actuator>::data_of(v22))->outer_epilog_update(delta_t);
            v22 = v22->m_next_T_internal;
        } while (&this->m_list_rbc_angular_actuator != (phys_free_list<rigid_body_constraint_angular_actuator> *)v22);
        i1 = &this->m_list_rbc_contact;
    }
    for (i6 = this->m_list_rbc_upright.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_upright != (phys_free_list<rigid_body_constraint_upright> *)i6;
        i6 = i6->m_next_T_internal)
    {
        ;
    }
    for (i7 = this->m_list_rbc_custom_orientation.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_custom_orientation != (phys_free_list<rigid_body_constraint_custom_orientation> *)i7;
        i7 = i7->m_next_T_internal)
    {
        ;
    }
    for (i8 = this->m_list_rbc_custom_path.m_dummy_head.m_next_T_internal;
        &this->m_list_rbc_custom_path != (phys_free_list<rigid_body_constraint_custom_path> *)i8;
        i8 = i8->m_next_T_internal)
    {
        ;
    }
    for (i9 = this->m_list_rbc_contact.m_dummy_head.m_next_T_internal;
        i1 != (phys_free_list<rigid_body_constraint_contact> *)i9;
        i9 = i9->m_next_T_internal)
    {
        ;
    }
}

physics_system *__cdecl physics_system::create_physics_system()
{
    physics_system *v0; // eax

    v0 = (physics_system *)PMM_PERM_ALLOCATE(sizeof(physics_system), 16);
    if (v0)
    {
        //return physics_system::physics_system(v0);
        return new (v0) physics_system;
    }
    else
        return 0;
}

void __cdecl physics_system::initialize()
{
    g_physics_system = physics_system::create_physics_system();
}

void __cdecl physics_system::destroy_physics_system(physics_system *psys)
{
    //physics_system::~physics_system(psys);
    psys->~physics_system();
}

void __cdecl physics_system::shutdown()
{
    //physics_system::~physics_system(g_physics_system);
    g_physics_system->~physics_system();
    g_physics_system = 0;
}

void __cdecl IPN_partition_process(const rigid_body_constraint *rbc, int *island_count)
{
    rigid_body *b2; // ecx
    rigid_body *m_partition_head; // esi
    rigid_body *v4; // edi

    b2 = rbc->b2;
    if ( rbc->b1 )
    {
        if ( b2 )
        {
            m_partition_head = rbc->b1->m_partition_node.m_partition_head;
            v4 = b2->m_partition_node.m_partition_head;
            if ( m_partition_head )
            {
                if ( v4 && m_partition_head != v4 )
                {
                    if ( --*island_count <= 0
                        && _tlAssert("source/physics_system_internal.cpp", 215, "*island_count > 0", "") )
                    {
                        __debugbreak();
                    }
                    if ( m_partition_head->m_partition_node.m_partition_size < v4->m_partition_node.m_partition_size )
                        IPN_merge(v4, m_partition_head);
                    else
                        IPN_merge(m_partition_head, v4);
                }
            }
        }
    }
}

void __cdecl SetIdentity(phys_mat44 *m)
{
    m->x.x = PHYS_IDENTITY_MATRIX.x.x;
    m->x.y = PHYS_IDENTITY_MATRIX.x.y;
    m->x.z = PHYS_IDENTITY_MATRIX.x.z;
    m->y.x = PHYS_IDENTITY_MATRIX.y.x;
    m->y.y = PHYS_IDENTITY_MATRIX.y.y;
    m->y.z = PHYS_IDENTITY_MATRIX.y.z;
    m->z.x = PHYS_IDENTITY_MATRIX.z.x;
    m->z.y = PHYS_IDENTITY_MATRIX.z.y;
    m->z.z = PHYS_IDENTITY_MATRIX.z.z;
    m->w.x = PHYS_IDENTITY_MATRIX.w.x;
    m->w.y = PHYS_IDENTITY_MATRIX.w.y;
    m->w.z = PHYS_IDENTITY_MATRIX.w.z;
}

void rigid_body::swap_last_position()
{
    float x; // eax
    double v3; // st7
    float y; // ecx
    float z; // edx
    double v6; // st7
    float v7; // [esp+10h] [ebp-10h]
    float v8; // [esp+10h] [ebp-10h]
    float v9; // [esp+10h] [ebp-10h]
    float v10; // [esp+10h] [ebp-10h]
    float v11; // [esp+10h] [ebp-10h]
    float v12; // [esp+10h] [ebp-10h]

    x = this->m_last_position.x;
    v3 = this->m_mat.w.x;
    y = this->m_last_position.y;
    z = this->m_last_position.z;
    this->m_last_position.x = v3;
    this->m_last_position.y = this->m_mat.w.y;
    this->m_last_position.z = this->m_mat.w.z;
    this->m_mat.w.x = x;
    this->m_mat.w.y = y;
    this->m_mat.w.z = z;
    v6 = 100000.0;
    v7 = fabs(this->m_last_position.x);
    if ( v7 > 100000.0
        || (v8 = fabs(this->m_last_position.y), v8 > 100000.0)
        || (v9 = fabs(this->m_last_position.z), v9 > 100000.0) )
    {
        phys_exec_debug_callback(this);
        v6 = 100000.0;
    }
    v10 = fabs(this->m_mat.w.x);
    if ( v10 > v6 || (v11 = fabs(this->m_mat.w.y), v11 > v6) || (v12 = fabs(this->m_mat.w.z), v6 < v12) )
        phys_exec_debug_callback(this);
}

void __cdecl rbint::prolog_frame_advance(user_rigid_body *rb, float delta_t)
{
    double v3; // st7
    int savedregs; // [esp+18h] [ebp+0h] BYREF
    float rba; // [esp+20h] [ebp+8h]
    float rbb; // [esp+20h] [ebp+8h]
    float rbc; // [esp+20h] [ebp+8h]
    float rbd; // [esp+20h] [ebp+8h]
    float rbe; // [esp+20h] [ebp+8h]
    float rbf; // [esp+20h] [ebp+8h]

    if ( (rb->m_flags & 0x20) == 0
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 37,
                 "rb->is_user_rigid_body()",
                 "") )
    {
        __debugbreak();
    }
    if ( !rb->m_dictator
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 38,
                 "rb->m_dictator",
                 "") )
    {
        __debugbreak();
    }
    nuge::calc_velocities(&rb->m_mat, rb->m_dictator, delta_t, &rb->m_t_vel, &rb->m_a_vel);
    v3 = 100000.0;
    rba = fabs(rb->m_t_vel.x);
    if ( rba > 100000.0 || (rbb = fabs(rb->m_t_vel.y), rbb > 100000.0) || (rbc = fabs(rb->m_t_vel.z), rbc > 100000.0) )
    {
        phys_exec_debug_callback(rb);
        v3 = 100000.0;
    }
    rbd = fabs(rb->m_a_vel.x);
    if ( rbd > v3 || (rbe = fabs(rb->m_a_vel.y), rbe > v3) || (rbf = fabs(rb->m_a_vel.z), v3 < rbf) )
        phys_exec_debug_callback(rb);
}

void __cdecl rbint::collision_prolog(user_rigid_body *rb, float delta_t)
{
    float v2; // [esp-20h] [ebp-2Ch]
    float v3; // [esp-1Ch] [ebp-28h]
    float v4; // [esp-18h] [ebp-24h]
    float v5; // [esp-4h] [ebp-10h]
    float v6; // [esp-4h] [ebp-10h]
    float v7; // [esp-4h] [ebp-10h]

    if ( (rb->m_flags & 0x20) == 0
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 46,
                 "rb->is_user_rigid_body()",
                 "") )
    {
        __debugbreak();
    }
    if ( !rb->m_dictator
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 47,
                 "rb->m_dictator",
                 "") )
    {
        __debugbreak();
    }
    v2 = rb->m_t_vel.x * delta_t;
    v3 = rb->m_t_vel.y * delta_t;
    v4 = delta_t * rb->m_t_vel.z;
    rb->m_moved_vec.x = v2;
    rb->m_moved_vec.y = v3;
    rb->m_moved_vec.z = v4;
    v5 = fabs(rb->m_moved_vec.x);
    if ( v5 > 100000.0 || (v6 = fabs(rb->m_moved_vec.y), v6 > 100000.0) || (v7 = fabs(rb->m_moved_vec.z), v7 > 100000.0) )
        phys_exec_debug_callback(rb);
    rb->m_smallest_lambda = 0.0;
}

void rbint::take_next_step(user_rigid_body *rb, float delta_t)
{
    phys_mat44 rv; // [esp+18h] [ebp-5Ch] BYREF
    float dx; // [esp+58h] [ebp-1Ch]
    float dy; // [esp+5Ch] [ebp-18h]
    float dz; // [esp+60h] [ebp-14h]
    //unsigned int v5[3]; // [esp+68h] [ebp-Ch] BYREF
    //_UNKNOWN *retaddr; // [esp+74h] [ebp+0h]
    //
    //v5[0] = a1;
    //v5[1] = retaddr;
    if ( (rb->m_flags & 0x20) == 0
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 65,
                 "rb->is_user_rigid_body()",
                 "") )
    {
        __debugbreak();
    }
    if ( !rb->m_dictator
        && _tlAssert(
                 "c:\\projects_pc\\cod\\codsrc\\tl\\physics\\include\\rigid_body_internal.h",
                 66,
                 "rb->m_dictator",
                 "") )
    {
        __debugbreak();
    }
    dx = rb->m_t_vel.x * delta_t;
    dy = rb->m_t_vel.y * delta_t;
    dz = rb->m_t_vel.z * delta_t;
    rb->m_mat.w.x = rb->m_mat.w.x + dx;
    rb->m_mat.w.y = rb->m_mat.w.y + dy;
    rb->m_mat.w.z = dz + rb->m_mat.w.z;
    make_rotate(&rv, &rb->m_a_vel, delta_t, 1000.0);
    phys_multiply_mat(&rb->m_mat, &rv, &rb->m_mat);
}

void __cdecl rbint::collision_prolog(rigid_body *const rb, float delta_t)
{
    float v2; // [esp-30h] [ebp-3Ch]
    float v3; // [esp-2Ch] [ebp-38h]
    float v4; // [esp-28h] [ebp-34h]
    float v5; // [esp-20h] [ebp-2Ch]
    float v6; // [esp-20h] [ebp-2Ch]
    float v7; // [esp-1Ch] [ebp-28h]
    float v8; // [esp-1Ch] [ebp-28h]
    float v9; // [esp-18h] [ebp-24h]
    float v10; // [esp-18h] [ebp-24h]
    float v11; // [esp-4h] [ebp-10h]
    float v12; // [esp-4h] [ebp-10h]
    float v13; // [esp-4h] [ebp-10h]

    v5 = rb->m_t_vel.x * delta_t;
    v7 = rb->m_t_vel.y * delta_t;
    v9 = delta_t * rb->m_t_vel.z;
    v2 = v5 + rb->m_mat.w.x;
    v3 = rb->m_mat.w.y + v7;
    v4 = rb->m_mat.w.z + v9;
    v6 = v2 - rb->m_last_position.x;
    v8 = v3 - rb->m_last_position.y;
    v10 = v4 - rb->m_last_position.z;
    rb->m_moved_vec.x = v6;
    rb->m_moved_vec.y = v8;
    rb->m_moved_vec.z = v10;
    v11 = fabs(rb->m_moved_vec.x);
    if ( v11 > 100000.0
        || (v12 = fabs(rb->m_moved_vec.y), v12 > 100000.0)
        || (v13 = fabs(rb->m_moved_vec.z), v13 > 100000.0) )
    {
        phys_exec_debug_callback(rb);
    }
    rb->m_smallest_lambda = 1.0;
    //rigid_body::swap_last_position(rb);
    rb->swap_last_position();
    rb->m_largest_vel_sq = 0.0;
}

void physics_system::validate_member(environment_rigid_body *rb)
{
    unsigned int m_flags; // edx

    if ( rb )
    {
        m_flags = rb->m_flags;
        if ( (m_flags & 0x10) != 0 )
        {
            iassert(rb == &m_environment_rigid_body);
        }
        else
        {
            //p_m_partition_head = (char *)rb - offsetof(phys_free_list<rigid_body>::T_internal, m_data);
            if ((m_flags & 0x20) != 0)
            {
                using TI = phys_free_list<user_rigid_body>::T_internal;
                static_assert(sizeof(TI) == BO1_ABI_SIZE(0x1D0, 544, 0x1D0, 528), "size mismatch");
                TI *ti = (TI *)((char *)rb - offsetof(TI, m_data));
                PMM_VALIDATE((char *)ti, sizeof(TI), phys_free_list<user_rigid_body>::T_internal_alignment());

                //PMM_VALIDATE(p_m_partition_head, sizeof(phys_free_list<user_rigid_body>::T_internal), phys_free_list<user_rigid_body>::T_internal_alignment());
            }
            else
            {
                using TI = phys_free_list<rigid_body>::T_internal;
                static_assert(sizeof(TI) == BO1_ARCH_SIZE(0x180, 464), "size mismatch");
                TI *ti = (TI *)((char *)rb - offsetof(TI, m_data));
                PMM_VALIDATE((char *)ti, sizeof(TI), phys_free_list<rigid_body>::T_internal_alignment());

                //PMM_VALIDATE(p_m_partition_head, sizeof(phys_free_list<rigid_body>::T_internal), phys_free_list<rigid_body>::T_internal_alignment());
            }
        }
    }
}

void physics_system::solver_priority_sort(phys_transient_allocator *transient_buffer)
{
    rigid_body_constraint_contact **v3; // edi

    if ( this->m_list_rbc_contact.m_list_count >= 4 )
    {
        if ( transient_buffer->m_cur )
        {
            if ( _tlAssert("source/physics_system_internal.cpp", 351, "transient_buffer->is_empty()", "") )
                __debugbreak();
        }
        //v3 = (rigid_body_constraint_contact **)phys_transient_allocator::allocate(
        //                                                                                 transient_buffer,
        //                                                                                 4 * this->m_list_rbc_contact.m_list_count,
        //                                                                                 4,
        //                                                                                 0,
        //                                                                                 "phys_transient_allocator out of memory.");
        v3 = (rigid_body_constraint_contact **)transient_buffer->allocate(sizeof(rigid_body_constraint_contact *) * this->m_list_rbc_contact.m_list_count, alignof(rigid_body_constraint_contact *), 0, "phs_Transient_allocator out of memory.");
        //phys_free_list<rigid_body_constraint_contact>::ptr_array_read(
        //    &this->m_list_rbc_contact,
        //    v3,
        //    this->m_list_rbc_contact.m_list_count);
        this->m_list_rbc_contact.ptr_array_read(v3, this->m_list_rbc_contact.m_list_count);
        merge_sort(v3, this->m_list_rbc_contact.m_list_count);
        //phys_free_list<rigid_body_constraint_contact>::ptr_array_write(
        //    &this->m_list_rbc_contact,
        //    v3,
        //    this->m_list_rbc_contact.m_list_count);
        this->m_list_rbc_contact.ptr_array_write(v3, this->m_list_rbc_contact.m_list_count);
        //phys_transient_allocator::reset(transient_buffer);
        transient_buffer->reset();
    }
}