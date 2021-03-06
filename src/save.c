/**
 * Copyright (c) 2006-2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#define _GNU_SOURCE
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>


#include "git-spot.h"
#include "cmd.h"

typedef void (*sg_callback) (void *user_data);

static void container_loaded(sp_playlistcontainer *pc, void *user_data);
static void save_playlist_async(sp_playlist *playlist, const char *directory,
    unsigned int prefix,
    sg_callback cb, void *user_data);

static char *safe_filename (const char *str)
{
  char *filename = strdup(str);
  char *c;

  for(c = filename; *c != 0; c++)
    {
      if(strchr("/\\. \t+$'\"", *c) != NULL)
        *c = '_';
    }

  return filename;
}

typedef struct _string_list string_list;

struct _string_list {
  string_list *next;
  string_list *prev;
  char *data;
};

string_list *
string_list_first(string_list *list)
{
  while(list != NULL && list->prev != NULL)
    list = list->prev;
  return list;
}

string_list *
string_list_last(string_list *list)
{
  while(list != NULL && list->next != NULL)
    list = list->next;
  return list;
}

string_list *
string_list_append(string_list *list, char *data)
{
  string_list *new_link = malloc(sizeof(string_list));
  new_link->next = NULL;
  new_link->data = data;

  while (list != NULL && list->next != NULL)
    list = list->next;

  new_link->prev = list;

  if(list != NULL)
    list->next = new_link;

  return new_link;
}

string_list *
string_list_copy(string_list *old_list)
{
  string_list *first_link = string_list_append(NULL, old_list->data);
  string_list *new_link = first_link;
  string_list *old_link = string_list_first(old_list);

  while (old_link != NULL)
    {
      new_link = string_list_append(new_link, strdup(old_link->data));
      old_link = old_link->next;
    }

  return first_link;
}

string_list *
string_list_remove_tail(string_list *list)
{
  string_list *new_tail;
  if (list == NULL)
    return NULL;
  list = string_list_last(list);
  new_tail = list->prev;
  new_tail->next = NULL;
  free(list->data);
  free(list);

  return new_tail;
}

static void
string_list_free(string_list *list)
{
  list = string_list_first(list);
  while(list != NULL)
    {
      string_list *next_list = list->next;
      free(list->data);
      free(list);
      list = next_list;
    }
}

static char *
string_list_join(string_list *list, const char *sep)
{
  string_list *l;
  char *old_string = NULL;
  char *new_string = NULL;

  for (l = string_list_first(list); l != NULL; l = l->next)
    {
      asprintf(&new_string, "%s%s%s", old_string, sep, l->data);
      free(old_string);
      old_string = new_string;
    }
  new_string = strdup(old_string + strlen("(null)") + strlen(sep));
  free(old_string);
  return new_string;
}

typedef struct _container_context container_context;

struct _container_context {
  sp_playlistcontainer *pc;
  char *name;
  sp_playlistcontainer_callbacks *callbacks;
  int started_calls;
  int finished_calls;
  void (*finally_func) (container_context *);
  void *user_data;
};

static container_context *container_context_new(
    sp_playlistcontainer *pc,
    const char *name,
    void *user_data)
{
  container_context *ctx = malloc(sizeof(container_context));
  ctx->pc = pc;
  ctx->name = strdup(name);
  ctx->callbacks = malloc(sizeof(sp_playlistcontainer_callbacks));
  memset(ctx->callbacks, 0, sizeof(sp_playlistcontainer_callbacks));
  ctx->started_calls = 0;
  ctx->finished_calls = 0;
  ctx->user_data = user_data;
  return ctx;
}

static void container_context_add_finally(
    container_context *ctx,
    void (*finally_func) (container_context *))
{
  ctx->finally_func = finally_func;

  if (ctx->started_calls == ctx->finished_calls)
    {
      printf("Already finished all %d calls.\n", ctx->finished_calls);
      finally_func(ctx);
    }
}

static void container_context_free(container_context *ctx) {
  sp_playlistcontainer_remove_callbacks(ctx->pc, ctx->callbacks, ctx);
  free(ctx->callbacks);
  free(ctx->name);
  free(ctx);
}

static int subscriptions_updated;

static void cmd_save_finally(container_context *ctx)
{
  cmd_save_social(0, NULL);
  container_context_free(ctx);
}

static container_context *container_context_start_call(container_context *ctx)
{
  ctx->started_calls ++;
  return ctx;
}

static void container_context_finish_call(container_context *ctx)
{
  ctx->finished_calls ++;
  printf("%d of %d calls finished.\n", ctx->finished_calls, ctx->started_calls);
  if(ctx->finished_calls == ctx->started_calls)
    {
      if (ctx->finally_func != NULL)
        ctx->finally_func(ctx);
      else
        printf("context's finally function is NULL.");
    }
}

/**
 *
 */
int cmd_save(int argc, char **argv)
{
  sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);
  container_context *ctx = NULL;

  if (argc < 1)
    ctx = container_context_new(pc, ".", NULL);
  else
    ctx = container_context_new(pc, argv[1], NULL);

  ctx->callbacks->container_loaded = container_loaded;
  sp_playlistcontainer_add_callbacks(pc, ctx->callbacks,
      container_context_start_call(ctx));
  container_context_add_finally(ctx, cmd_save_finally);
  return 1;
}

static void container_loaded(sp_playlistcontainer *pc, void *userdata)
{
  container_context *ctx = userdata;
  int i, j, level = 0;
  unsigned int prefix = 0;
  sp_playlist *pl;
  char name[200];
  string_list *path = string_list_append(NULL, strdup(ctx->name));

  if(mkdir(ctx->name, 0755) != 0 && errno != EEXIST)
    printf("WARNING: mkdir(\"%s\") failed.", ctx->name);

  printf("path = %s\n", ctx->name);
  printf("%d entries in the container\n", sp_playlistcontainer_num_playlists(pc));

  sp_session_num_friends(g_session);

  for (i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
    char *folder_name;
    switch (sp_playlistcontainer_playlist_type(pc, i)) {
      case SP_PLAYLIST_TYPE_PLAYLIST:
        printf("%d. ", i);
        folder_name = string_list_join(path, "/");
        if(mkdir(folder_name, 0755) != 0 && errno != EEXIST)
          printf("WARNING: mkdir(\"%s\") failed.", folder_name);
        prefix ++;
        pl = sp_playlistcontainer_playlist(pc, i);
        save_playlist_async(pl, folder_name, prefix, (sg_callback)container_context_finish_call,
            container_context_start_call(ctx));
        free(folder_name);
        printf("%s", sp_playlist_name(pl));
        if(subscriptions_updated)
          printf(" (%d subscribers)", sp_playlist_num_subscribers(pl));
        printf("\n");
        break;
      case SP_PLAYLIST_TYPE_START_FOLDER:
        printf("%d. ", i);
        for (j = level; j; --j) printf("\t");
        sp_playlistcontainer_playlist_folder_name(pc, i, name, sizeof(name));
        printf("Folder: %s with id %lu\n", name,
             sp_playlistcontainer_playlist_folder_id(pc, i));
        level++;
        prefix = 0;
        path = string_list_append(path, safe_filename(name));

        folder_name = string_list_join(path, "/");
        if(mkdir(folder_name, 0755) != 0 && errno != EEXIST)
          printf("WARNING: mkdir(\"%s\") failed.", folder_name);
        free(folder_name);
        break;
      case SP_PLAYLIST_TYPE_END_FOLDER:
        path = string_list_remove_tail(path);
        level--;
        prefix = 0;
         printf("%d. ", i);
        for (j = level; j; --j) printf("\t");
        printf("End folder with id %lu\n",  sp_playlistcontainer_playlist_folder_id(pc, i));
        break;
      case SP_PLAYLIST_TYPE_PLACEHOLDER:
        printf("%d. Placeholder", i);
        break;
    }
  }

  printf("Made %d async calls.\n", ctx->started_calls -1);
  container_context_finish_call(ctx);
  string_list_free(path);
}

typedef struct {
  sp_playlist *playlist;
  char *directory;
  unsigned int prefix;
  sg_callback cb;
  void *user_data;
  sp_playlist_callbacks *callbacks;
} playlist_data;

static playlist_data *playlist_data_new(sp_playlist *playlist,
    const char *directory,
    unsigned int prefix,
    sg_callback cb,
    void *user_data)
{
  playlist_data *data = malloc(sizeof(playlist_data));

  data->playlist = playlist;
  data->directory = strdup(directory);
  data->prefix = prefix;
  data->cb = cb;
  data->user_data = user_data;
  data->callbacks = malloc(sizeof(sp_playlist_callbacks));
  memset(data->callbacks, 0, sizeof(sp_playlist_callbacks));

  return data;
}

static void playlist_data_free(playlist_data *data)
{
  sp_playlist_remove_callbacks(data->playlist, data->callbacks, data);
  free(data->directory);
  free(data->callbacks);
  free(data);
}

static void save_playlist_finally(playlist_data *data)
{
  if(data->cb != NULL)
    data->cb(data->user_data);
  playlist_data_free(data);
}

static char *
sg_link_dup_string(sp_link *link)
{
  char link_str[100];

  if(!sp_link_as_string(link, link_str, 100))
    printf("WARNING: sp_link_as_string failed.\n");

  return strdup(link_str);
}

/*
 * http://open.spotify.com/user/alsuren/playlist/1ruXh4qLoLj8GDWpSbYFsf
 * spotify:user:alsuren:playlist:1ruXh4qLoLj8GDWpSbYFsf
 */
static char *
sg_link_dup_http_string(sp_link *link)
{
  char link_str[100];
  char *c;


  if(!sp_link_as_string(link, link_str + strlen("http://open..com"), 100))
    printf("WARNING: sp_link_as_string failed.\n");

  memmove(link_str, "http://open.spotify.com", strlen("http://open.spotify.com"));

  for(c = link_str + strlen("http://open.spotify.com"); *c != 0; c ++)
    if(*c == ':')
      *c = '/';
  return strdup(link_str);
}

static void actually_save_playlist(playlist_data *data)
{
  int i;
  char *basename = safe_filename(sp_playlist_name(data->playlist));
   FILE *output;
  char *filename;
  sp_link *playlist_link = sp_link_create_from_playlist(data->playlist);
  char *playlist_http_link = sg_link_dup_http_string(playlist_link);
  char *playlist_uri_link = sg_link_dup_string(playlist_link);

  asprintf(&filename, "%s/%03u--%s--%s.json", data->directory, data->prefix, basename, playlist_uri_link);

  printf("Playlist '%s' ready.\n", sp_playlist_name(data->playlist));

  output = fopen(filename, "w+");
  free(basename);
  free(filename);


  fprintf(output, "{\"playlist_name\": \"%s\",\n"
      "\"http_link\": \"%s\",\n"
      "\"spotify_link\": \"%s\",\n"
      "\"songs\": [\n",
      sp_playlist_name(data->playlist),
      playlist_http_link, playlist_uri_link);

  for(i=0; i<sp_playlist_num_tracks(data->playlist); i++)
    {
      sp_track *track = sp_playlist_track(data->playlist, i);
      sp_link *link = sp_link_create_from_track(track, 0);
      int j = 0;
      char link_str[100];
      char *artists_str = NULL;
      sp_album *album;
      const char *album_str = NULL;

      if(!sp_link_as_string(link, link_str, 100))
        printf("WARNING: sp_link_as_string failed.\n");

      for(j=0; j < sp_track_num_artists(track); j++)
        {
          char *new_artists_str = NULL;
          sp_artist *artist = sp_track_artist(track, j);
          if (-1 == asprintf(&new_artists_str, "%s, \"%s\"",
              artists_str, sp_artist_name(artist)))
            {
              printf("WARNING: asprinf failed.\n");
              break;
            }
          free(artists_str);
          artists_str = new_artists_str;
        }
      if (artists_str == NULL)
        artists_str = "(null), \"Dunno yet.\"";

      album = sp_track_album(track);
      if(album != NULL && sp_album_is_loaded(album))
        album_str = sp_album_name(album);
      else
        album_str = "Dunno yet.";

      fprintf(output, "{\"name\": \"%s\", \"artists\": [%s], \"album\": \"%s\", "
          "\"duration\": %d, \"link\": \"%s\"},\n",
          sp_track_name(track), artists_str + 8 /* strip off "(null), "*/,
          album_str, sp_track_duration(track), link_str);
    }

  fprintf(output, "]}\n");
  fclose(output);
  save_playlist_finally(data);
}

static void playlist_state_changed_cb(sp_playlist *pl, void *userdata)
{
  playlist_data *data = userdata;
  if (sp_playlist_is_loaded(data->playlist))
    actually_save_playlist(data);
}

static void save_playlist_async(sp_playlist *playlist,
    const char *directory,
    unsigned int prefix,
    sg_callback cb,
    void *user_data)
{
  playlist_data *data = playlist_data_new(playlist, directory, prefix, cb, user_data);
  data->callbacks->playlist_state_changed = playlist_state_changed_cb;
  sp_playlist_add_callbacks(data->playlist, data->callbacks, data);

  playlist_state_changed_cb(data->playlist, data);
}

typedef struct {
  int started_calls;
  int finished_calls;
} save_social_context;

save_social_context *save_social_context_new()
{
  save_social_context *ctx = malloc(sizeof(save_social_context));

  ctx->started_calls = 0;
  ctx->finished_calls = 0;

  return ctx;
}

static void
save_social_context_free(save_social_context *ctx)
{
  free(ctx);
}

static void save_social_finally (save_social_context *ctx)
{
  char *argv[2];
  argv[0] = "";
  argv[1] = "veemcgee";
  // cmd_published_playlists(2, &argv);
  cmd_logout(0, NULL);
  save_social_context_free(ctx);
}

static save_social_context *save_social_context_start_call(
    save_social_context *ctx)
{
  ctx->started_calls ++;
  return ctx;
}

static void finish_with_user (container_context *ctx)
{
  save_social_context *save_ctx = ctx->user_data;

  save_ctx->finished_calls ++;
  if(save_ctx->started_calls == save_ctx->finished_calls)
    save_social_finally(save_ctx);
}
/**
 *
 */
int cmd_save_social(int argc, char **argv)
{
  int i;
  int num_friends;
  save_social_context *save_ctx = save_social_context_new();


  num_friends = sp_session_num_friends(g_session);
  printf("saving playlists for %d friends.\n", num_friends);
  for(i = -1; i < num_friends; i++) {
    // sp_user *user = sp_session_friend(g_session, i);
    const char *name = "vmcgee"; // sp_user_canonical_name(user);
    sp_playlistcontainer *pc = sp_session_publishedcontainer_for_user_create(
        g_session, name);
    container_context *ctx = container_context_new(pc, name,
        save_social_context_start_call(save_ctx));
    printf("saving playlists for %s.\n", name);

    container_context_start_call(ctx);
    ctx->callbacks->container_loaded = container_loaded;
    sp_playlistcontainer_add_callbacks(pc, ctx->callbacks, ctx);

    container_context_add_finally(ctx, finish_with_user);
  }

  if (save_ctx->started_calls == 0)
    save_social_finally(save_ctx);

  return 1;
}


/**
 *
 */
int cmd_load(int argc, char **argv)
{
  int index, i;
  sp_track *track;
  sp_playlist *playlist;
  sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);

  if (argc < 1) {
    printf("playlist [playlist index]\n");
    return 0;
  }

  index = atoi(argv[1]);
  if (index < 0 || index > sp_playlistcontainer_num_playlists(pc)) {
    printf("invalid index\n");
    return 0;
  }
  playlist = sp_playlistcontainer_playlist(pc, index);
  printf("Playlist %s by %s%s%s\n",
       sp_playlist_name(playlist),
       sp_user_display_name(sp_playlist_owner(playlist)),
       sp_playlist_is_collaborative(playlist) ? " (collaborative)" : "",
       sp_playlist_has_pending_changes(playlist) ? " with pending changes" : ""
       );
  for (i = 0; i < sp_playlist_num_tracks(playlist); ++i) {
    track = sp_playlist_track(playlist, i);
    printf("%d. %c %s%s %s\n", i,
         sp_track_is_starred(g_session, track) ? '*' : ' ',
         sp_track_is_local(g_session, track) ? "local" : "     ",
         sp_track_is_autolinked(g_session, track) ? "autolinked" : "          ",
         sp_track_name(track));
  }
  return 1;
}

