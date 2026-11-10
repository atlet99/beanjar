// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests of the media pool's access rules (grants.c): EXPORT's range checks against the
 * directory, and CLAIM's bookkeeping. Built against a kernel with KUnit (the debug kernel) and
 * loaded in the guest; its TAP goes to the kernel log, which tools/testing/kunit/kunit.py parse
 * reads (tests/run-kernel-tests.sh).
 */
#include <kunit/test.h>
#include <linux/slab.h>
#include "../grants.c"

#define G    WEBOS_MEDIA_POOL_GRANULE
#define SIZE WEBOS_MEDIA_POOL_SIZE

static struct webos_media_pool_dir *new_dir(struct kunit *test)
{
    struct webos_media_pool_dir *d = kunit_kzalloc(test, sizeof(*d), GFP_KERNEL);

    KUNIT_ASSERT_NOT_NULL(test, d);
    d->magic = WEBOS_MEDIA_POOL_MAGIC;
    d->version = WEBOS_MEDIA_POOL_VERSION;
    d->size = SIZE;
    return d;
}

static void grant(struct webos_media_pool_dir *d, u32 port, int e, u32 off, u32 size, u32 state,
                  u32 gen)
{
    d->port[port].extent[e] = (struct webos_media_extent){off, size, state, gen};
}

static void lookup_in_range(struct kunit *test)
{
    struct webos_media_pool_dir *d = new_dir(test);
    u32 gen = 0;

    grant(d, 2, 1, 4 * G, 2 * G, WEBOS_MEDIA_EXTENT_LIVE, 7);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 4 * G, 2 * G, &gen), 1);
    KUNIT_EXPECT_EQ(test, gen, 7u);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 5 * G, PAGE_SIZE, &gen), 1);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 6 * G - PAGE_SIZE, PAGE_SIZE, &gen), 1);
}

static void lookup_refusals(struct kunit *test)
{
    struct webos_media_pool_dir *d = new_dir(test);
    u32 gen;

    grant(d, 2, 0, 4 * G, 2 * G, WEBOS_MEDIA_EXTENT_LIVE, 1);
    /* past the extent, before it, straddling its end */
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 6 * G, PAGE_SIZE, &gen), -EPERM);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 3 * G, PAGE_SIZE, &gen), -EPERM);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 6 * G - PAGE_SIZE, 2 * PAGE_SIZE, &gen),
                    -EPERM);
    /* another port's extent */
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 3, 4 * G, PAGE_SIZE, &gen), -EPERM);
    /* empty, unaligned, beyond the pool, wrapping, a port out of range */
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 4 * G, 0, &gen), -EINVAL);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 4 * G + 1, PAGE_SIZE, &gen), -EINVAL);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, SIZE - PAGE_SIZE, 2 * PAGE_SIZE, &gen),
                    -EINVAL);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 2, 0xfffff000u, 0x2000, &gen), -EINVAL);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, WEBOS_MEDIA_PORTS, 4 * G, PAGE_SIZE, &gen),
                    -EINVAL);
}

static void lookup_states(struct kunit *test)
{
    struct webos_media_pool_dir *d = new_dir(test);
    u32 gen;

    grant(d, 0, 0, 4 * G, G, WEBOS_MEDIA_EXTENT_RETIRING, 1);
    grant(d, 0, 1, 8 * G, G, WEBOS_MEDIA_EXTENT_FREE, 2);
    grant(d, 0, 2, 12 * G, G, 99, 3);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 0, 4 * G, PAGE_SIZE, &gen), -EPERM);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 0, 8 * G, PAGE_SIZE, &gen), -EPERM);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 0, 12 * G, PAGE_SIZE, &gen), -EPERM);
}

static void lookup_hostile_directory(struct kunit *test)
{
    struct webos_media_pool_dir *d = new_dir(test);
    u32 gen;

    /* the directory itself, an extent past the pool's end, one that wraps */
    grant(d, 1, 0, 0, 2 * G, WEBOS_MEDIA_EXTENT_LIVE, 1);
    grant(d, 1, 1, SIZE - G, 4 * G, WEBOS_MEDIA_EXTENT_LIVE, 2);
    grant(d, 1, 2, 0xffff0000u, 0x20000, WEBOS_MEDIA_EXTENT_LIVE, 3);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 1, 0, PAGE_SIZE, &gen), -EPERM);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 1, G, PAGE_SIZE, &gen), -EPERM);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, SIZE, 1, SIZE - G, PAGE_SIZE, &gen), -EPERM);
    /* a smaller pool than the directory's extents assume */
    grant(d, 1, 3, 32 * G, G, WEBOS_MEDIA_EXTENT_LIVE, 4);
    KUNIT_EXPECT_EQ(test, mp_grant_lookup(d, 16 * G, 1, 32 * G, PAGE_SIZE, &gen), -EINVAL);
}

static void extent_live(struct kunit *test)
{
    struct webos_media_pool_dir *d = new_dir(test);

    grant(d, 4, 3, 4 * G, G, WEBOS_MEDIA_EXTENT_LIVE, 5);
    KUNIT_EXPECT_TRUE(test, mp_extent_live(d, 4, 3, 5));
    KUNIT_EXPECT_FALSE(test, mp_extent_live(d, 4, 3, 6)); /* granted again meanwhile */
    d->port[4].extent[3].state = WEBOS_MEDIA_EXTENT_RETIRING;
    KUNIT_EXPECT_FALSE(test, mp_extent_live(d, 4, 3, 5));
}

static void claims_exclusive(struct kunit *test)
{
    const void *owner[WEBOS_MEDIA_PORTS] = {};
    u32 claims[WEBOS_MEDIA_PORTS] = {};
    int a = -1, b = -1;
    char fa, fb;

    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fa, &a, 3), 0);
    KUNIT_EXPECT_EQ(test, claims[3], 1u);
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fa, &a, 3), 0); /* again: no change */
    KUNIT_EXPECT_EQ(test, claims[3], 1u);
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fb, &b, 3), -EBUSY);
    KUNIT_EXPECT_EQ(test, b, -1);
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fb, &b, WEBOS_MEDIA_PORTS), -EINVAL);
    /* a moves to 5: 3 ends (even), 5 begins (odd), and b may have 3 */
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fa, &a, 5), 0);
    KUNIT_EXPECT_EQ(test, claims[3], 2u);
    KUNIT_EXPECT_EQ(test, claims[5], 1u);
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fb, &b, 3), 0);
    KUNIT_EXPECT_EQ(test, claims[3], 3u);
}

static void claims_release(struct kunit *test)
{
    const void *owner[WEBOS_MEDIA_PORTS] = {};
    u32 claims[WEBOS_MEDIA_PORTS] = {};
    int a = -1, none = -1;
    char fa, fb;

    KUNIT_EXPECT_EQ(test, mp_unclaim(owner, claims, &fa, &none), -1);
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fa, &a, 0), 0);
    KUNIT_EXPECT_EQ(test, mp_unclaim(owner, claims, &fa, &a), 0);
    KUNIT_EXPECT_EQ(test, claims[0], 2u);
    KUNIT_EXPECT_EQ(test, a, -1);
    KUNIT_EXPECT_PTR_EQ(test, owner[0], (const void *)NULL);
    /* the port is free for another file */
    KUNIT_EXPECT_EQ(test, mp_claim(owner, claims, &fb, &none, 0), 0);
}

static struct kunit_case media_pool_cases[] = {
    KUNIT_CASE(lookup_in_range), KUNIT_CASE(lookup_refusals),
    KUNIT_CASE(lookup_states),   KUNIT_CASE(lookup_hostile_directory),
    KUNIT_CASE(extent_live),     KUNIT_CASE(claims_exclusive),
    KUNIT_CASE(claims_release),  {}};

static struct kunit_suite media_pool_suite = {
    .name = "media-pool",
    .test_cases = media_pool_cases,
};

kunit_test_suite(media_pool_suite);
MODULE_DESCRIPTION("KUnit tests of the media pool's access rules");
MODULE_LICENSE("GPL");
