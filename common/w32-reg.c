/* w32-reg.c -  MS-Windows Registry access
 * Copyright (C) 1999, 2002, 2007 Free Software Foundation, Inc.
 *
 * This file is part of JNLIB, which is a subsystem of GnuPG.
 *
 * JNLIB is free software; you can redistribute it and/or modify it
 * under the terms of either
 *
 *   - the GNU Lesser General Public License as published by the Free
 *     Software Foundation; either version 3 of the License, or (at
 *     your option) any later version.
 *
 * or
 *
 *   - the GNU General Public License as published by the Free
 *     Software Foundation; either version 2 of the License, or (at
 *     your option) any later version.
 *
 * or both in parallel, as here.
 *
 * JNLIB is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copies of the GNU General Public License
 * and the GNU Lesser General Public License along with this program;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#ifdef HAVE_W32_SYSTEM
 /* This module is only used in this environment */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>

#include "libjnlib-config.h"
#include "utf8conv.h"
#include "w32help.h"


static HKEY
get_root_key(const char *root)
{
  HKEY root_key;

  if (!root)
    root_key = HKEY_CURRENT_USER;
  else if (!strcmp( root, "HKEY_CLASSES_ROOT" ) )
    root_key = HKEY_CLASSES_ROOT;
  else if (!strcmp( root, "HKEY_CURRENT_USER" ) )
    root_key = HKEY_CURRENT_USER;
  else if (!strcmp( root, "HKEY_LOCAL_MACHINE" ) )
    root_key = HKEY_LOCAL_MACHINE;
  else if (!strcmp( root, "HKEY_USERS" ) )
    root_key = HKEY_USERS;
  else if (!strcmp( root, "HKEY_PERFORMANCE_DATA" ) )
    root_key = HKEY_PERFORMANCE_DATA;
  else if (!strcmp( root, "HKEY_CURRENT_CONFIG" ) )
    root_key = HKEY_CURRENT_CONFIG;
  else
    return NULL;

  return root_key;
}


/* Return a string from the Win32 Registry or NULL in case of error.
   Caller must release the return value.  A NULL for root is an alias
   for HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE in turn.  */
char *
read_w32_registry_string (const char *root, const char *dir, const char *name)
{
#ifdef HAVE_W32CE_SYSTEM
  HKEY root_key, key_handle;
  DWORD n1, nbytes, type;
  char *result = NULL;
  wchar_t *wdir, *wname;

  if ( !(root_key = get_root_key(root) ) )
    return NULL;

  wdir = utf8_to_wchar (dir);
  if (!wdir)
    return NULL;

  if (RegOpenKeyEx (root_key, wdir, 0, KEY_READ, &key_handle) )
    {
      if (root)
        {
          jnlib_free (wdir);
          return NULL; /* No need for a RegClose, so return immediately. */
        }
      /* It seems to be common practise to fall back to HKLM. */
      if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, wdir, 0, KEY_READ, &key_handle) )
        {
          jnlib_free (wdir);
          return NULL; /* Still no need for a RegClose. */
        }
    }
  jnlib_free (wdir);

  if (name)
    {
      wname = utf8_to_wchar (name);
      if (!wname)
        goto leave;
    }
  else
    wname = NULL;

  nbytes = 2;
  if (RegQueryValueEx (key_handle, wname, 0, NULL, NULL, &nbytes))
    goto leave;
  result = jnlib_malloc ((n1=nbytes+2));
  if (!result)
    goto leave;
  if (RegQueryValueEx (key_handle, wname, 0, &type, result, &n1))
    {
      jnlib_free (result);
      result = NULL;
      goto leave;
    }
  result[nbytes] = 0;   /* Make sure it is a string.  */
  result[nbytes+1] = 0;
  if (type == REG_SZ || type == REG_EXPAND_SZ)
    {
      wchar_t *tmp = (void*)result;
      result = wchar_to_utf8 (tmp);
      jnlib_free (tmp);
    }

 leave:
  jnlib_free (wname);
  RegCloseKey (key_handle);
  return result;
#else /*!HAVE_W32CE_SYSTEM*/
  HKEY root_key, key_handle;
  DWORD n1, nbytes, type;
  char *result = NULL;

  if ( !(root_key = get_root_key(root) ) )
    return NULL;

  if (RegOpenKeyEx (root_key, dir, 0, KEY_READ, &key_handle) )
    {
      if (root)
        return NULL; /* No need for a RegClose, so return immediately. */
      /* It seems to be common practise to fall back to HKLM. */
      if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, dir, 0, KEY_READ, &key_handle) )
        return NULL; /* Still no need for a RegClose. */
    }

  nbytes = 1;
  if (RegQueryValueEx( key_handle, name, 0, NULL, NULL, &nbytes ) )
    goto leave;
  result = jnlib_malloc ((n1=nbytes+1));
  if (!result)
    goto leave;
  if (RegQueryValueEx( key_handle, name, 0, &type, result, &n1 ))
    {
      jnlib_free (result);
      result = NULL;
      goto leave;
    }
  result[nbytes] = 0; /* Make sure it is a string.  */
  if (type == REG_EXPAND_SZ && strchr (result, '%'))
    {
      char *tmp;

      n1 += 1000;
      tmp = jnlib_malloc (n1+1);
      if (!tmp)
        goto leave;
      nbytes = ExpandEnvironmentStrings (result, tmp, n1);
      if (nbytes && nbytes > n1)
        {
          jnlib_free (tmp);
          n1 = nbytes;
          tmp = jnlib_malloc (n1 + 1);
          if (!tmp)
            goto leave;
          nbytes = ExpandEnvironmentStrings (result, tmp, n1);
          if (nbytes && nbytes > n1)
            {
              /* Oops - truncated, better don't expand at all.  */
              jnlib_free (tmp);
              goto leave;
            }
          tmp[nbytes] = 0;
          jnlib_free (result);
          result = tmp;
        }
      else if (nbytes)
        {
          /* Okay, reduce the length.  */
          tmp[nbytes] = 0;
          jnlib_free (result);
          result = jnlib_malloc (strlen (tmp)+1);
          if (!result)
            result = tmp;
            else
              {
                strcpy (result, tmp);
                jnlib_free (tmp);
              }
        }
      else
        {
          /* Error - don't expand.  */
          jnlib_free (tmp);
        }
    }

 leave:
  RegCloseKey (key_handle);
  return result;
#endif /*!HAVE_W32CE_SYSTEM*/
}


/* Note: This code is not well tested.  However, it is not used in
   GnuPG.  */
#if 0
int
write_w32_registry_string (const char *root, const char *dir,
                           const char *name, const char *value)
{
  HKEY root_key, reg_key;
#ifdef HAVE_W32CE_SYSTEM
  wchar_t *wdir, *wname, *wvalue;
  DWORD disp;

  if ( !(root_key = get_root_key(root) ) )
    return -1;

  wdir = utf8_to_wchar (dir);
  if (!wdir)
    return -1;

  if (RegOpenKeyEx (root_key, wdir, 0, 0, &reg_key))
    {
      jnlib_free (wdir);
      return -1;
    }
  jnlib_free (wdir);

  if (name)
    {
      wname = utf8_to_wchar (name);
      if (!wname)
        return -1;
    }
  else
    wname = NULL;

  wvalue = utf8_to_wchar (value);
  if (wvalue)
    {
      jnlib_free (wname);
      return -1;
    }

  if (RegSetValueEx (reg_key, wname, 0, REG_SZ,
                     (BYTE *)wvalue, wcslen (wvalue)) != ERROR_SUCCESS )
    {

      if (RegCreateKeyEx (root_key, wname, 0, NULL, 0, 0, NULL,
                          &reg_key, &disp) != ERROR_SUCCESS)
        {
          RegCloseKey(reg_key);
          jnlib_free (wname);
          jnlib_free (wvalue);
          return -1;
        }
      if (RegSetValueEx (reg_key, wname, 0, REG_SZ,
                         (BYTE *)wvalue, wcslen (wvalue)) != ERROR_SUCCESS )
        {
          RegCloseKey(reg_key);
          jnlib_free (wname);
          jnlib_free (wvalue);
          return -1;
        }
    }

  jnlib_free (wname);
  jnlib_free (wvalue);
  RegCloseKey (reg_key);
  return 0;
#else /*!HAVE_W32CE_SYSTEM*/

  if ( !(root_key = get_root_key(root) ) )
    return -1;

  if ( RegOpenKeyEx( root_key, dir, 0, KEY_WRITE, &reg_key )
       != ERROR_SUCCESS )
    return -1;

  if ( RegSetValueEx (reg_key, name, 0, REG_SZ, (BYTE *)value,
                      strlen( value ) ) != ERROR_SUCCESS )
    {
      if ( RegCreateKey( root_key, name, &reg_key ) != ERROR_SUCCESS )
        {
          RegCloseKey(reg_key);
          return -1;
        }
      if ( RegSetValueEx (reg_key, name, 0, REG_SZ, (BYTE *)value,
                          strlen( value ) ) != ERROR_SUCCESS )
        {
          RegCloseKey(reg_key);
          return -1;
        }
    }

  RegCloseKey (reg_key);
  return 0;
#endif /*!HAVE_W32CE_SYSTEM*/
}
#endif /*0*/
#endif /*HAVE_W32_SYSTEM*/
