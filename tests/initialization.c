/*
 * Copyright (C) Navaneeth.K.N
 *
 * This is part of libvarnam. See LICENSE.txt for the license
 */


#include <check.h>
#include "testcases.h"
#include "../varnam.h"

START_TEST (set_scheme_details)
{
    int rc;
    char *msg, *filename;
    varnam *handle;

    filename = get_unique_filename();
    rc = varnam_init (filename, &handle, &msg);
    assert_success (rc);

    rc = varnam_set_scheme_details(handle, "ml", "ml-unicode", "Malayalam", "Navaneeth K N", "May 3 2012");
    assert_success (rc);

    ck_assert_str_eq (varnam_get_scheme_language_code (handle), "ml");
    ck_assert_str_eq (varnam_get_scheme_identifier (handle), "ml-unicode");
    ck_assert_str_eq (varnam_get_scheme_display_name (handle), "Malayalam");
    ck_assert_str_eq (varnam_get_scheme_author (handle), "Navaneeth K N");
    ck_assert_str_eq (varnam_get_scheme_compiled_date (handle), "May 3 2012");

    varnam_destroy (handle);
    free (filename);
}
END_TEST

START_TEST (enable_suggestions)
{
    int rc;
    char *msg, *filename;
    varnam *handle;

    filename = get_unique_filename();
    rc = varnam_init(filename, &handle, &msg);
    assert_success (rc);

    rc = varnam_config (handle, VARNAM_CONFIG_ENABLE_SUGGESTIONS, "output/00-suggestions");
    assert_success (rc);

    varnam_destroy (handle);
    free (filename);
}
END_TEST

START_TEST (file_exists)
{
    int rc;
    char *msg, *filename;
    varnam *handle;

    filename = get_unique_filename();
    rc = varnam_init(filename, &handle, &msg);
    assert_success(rc);

    rc = is_file_exists("output/00-suggestions");
    assert_success(rc);

    rc = is_file_exists("output/doesnotexist");
    assert_success(!rc);

    rc = is_file_exists("output");
    assert_success(!rc);    
}
END_TEST

START_TEST (normal_init)
{
    int rc;
    char *msg, *filename;
    varnam *handle;

    filename = get_unique_filename();
    rc = varnam_init(filename, &handle, &msg);
    assert_success (rc);

    if (handle->internal->config_use_dead_consonants != 0)
    {
        ck_abort_msg ("varnam_init() should have turned off use dead consonant option");
    }

    rc = varnam_config (handle, VARNAM_CONFIG_USE_DEAD_CONSONANTS, 0);
    assert_success (rc);

    if (handle->internal->config_use_dead_consonants != 0)
    {
        ck_abort_msg ("varnam_config() is not changing value of use_dead_consonant option");
    }

    varnam_destroy (handle);
    free (filename);
}
END_TEST

START_TEST (initialize_using_lang_code)
{
  int rc;
  char *errMsg = NULL;
  varnam *handle;
  strbuf *tmp;

  rc = varnam_init_from_lang ("ml", &handle, &errMsg);
  if (errMsg != NULL) {
    printf ("init_from_lang failed: %s\n", errMsg);
  }

  assert_success (rc);
  ck_assert_str_eq ("/usr/local/share/varnam/vst/ml.vst", varnam_get_scheme_file (handle));
  tmp = strbuf_init (10);
  strbuf_addf (tmp, "%s/.local/share/varnam/suggestions/ml.vst.learnings", getenv ("HOME"));
  ck_assert_str_eq (strbuf_to_s (tmp), varnam_get_suggestions_file (handle));

  strbuf_destroy (tmp);
  varnam_destroy (handle);
}
END_TEST

START_TEST (initialize_using_invalid_lang_code)
{
  int rc;
  char *errMsg = NULL;
  varnam *handle;
  rc = varnam_init_from_lang ("mll", &handle, &errMsg);
  assert_error (rc);
  ck_assert (errMsg != NULL);
  varnam_destroy (handle);
}
END_TEST

START_TEST (initialize_on_writeprotected_location)
{
    int rc;
    char *msg;
    varnam *handle;

    const char *filename = "/etc/varnam-test.vst";
    rc = varnam_init (filename, &handle, &msg);
    if (rc != VARNAM_STORAGE_ERROR)
    {
        ck_abort_msg ("VARNAM_STORAGE_ERROR expected. Never got");
    }

    free (msg);
    varnam_destroy (handle);
}
END_TEST

START_TEST (initialize_on_incorrect_location)
{
    int rc;
    char *msg;
    varnam *handle;

    const char *filename = "invalid-dir/varnam-test.vst";
    rc = varnam_init(filename, &handle, &msg);
    if (rc != VARNAM_STORAGE_ERROR)
    {
        ck_abort_msg ("VARNAM_STORAGE_ERROR expected. Never got");
    }

    free (msg);
    varnam_destroy (handle);
}
END_TEST

START_TEST (initialize_on_already_existing_file)
{
    int rc;
    char *msg = NULL;
    varnam *handle = NULL;

    const char *filename = "initialization.c";
    rc = varnam_init(filename, &handle, &msg);
    if (rc != VARNAM_STORAGE_ERROR)
    {
        ck_abort_msg ("VARNAM_STORAGE_ERROR expected. Never got");
    }

    free (msg);
    varnam_destroy (handle);
}
END_TEST

START_TEST (init_destroy_loop_memory_stress_test)
{
    int rc, i;
    char* msg;
    varnam* handle;
    varray* output;

    for (i = 0; i < 100; i++) {
        rc = varnam_init ("../schemes/ml.vst", &handle, &msg);
        ck_assert_int_eq (rc, VARNAM_SUCCESS);
        rc = varnam_transliterate(handle, "navaneeth", &output);
        ck_assert_int_eq (rc, VARNAM_SUCCESS);
        varnam_destroy (handle);
    }
}
END_TEST

TCase* get_initialization_tests()
{
    TCase* tcase = tcase_create("initialization");
    tcase_add_test (tcase, set_scheme_details);
    tcase_add_test (tcase, enable_suggestions);
    tcase_add_test (tcase, file_exists);
    tcase_add_test (tcase, normal_init);
    tcase_add_test (tcase, initialize_using_lang_code);
    tcase_add_test (tcase, initialize_using_invalid_lang_code);
    tcase_add_test (tcase, initialize_on_writeprotected_location);
    tcase_add_test (tcase, initialize_on_incorrect_location);
    tcase_add_test (tcase, initialize_on_already_existing_file);
    tcase_add_test (tcase, init_destroy_loop_memory_stress_test);
    return tcase;
}
