// Copyright (c) 2014 Greenheart Games Pty. Ltd. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SRC_GREENWORKS_WORKSHOP_WORKERS_H_
#define SRC_GREENWORKS_WORKSHOP_WORKERS_H_

#include "steam_async_worker.h"

#include <map>
#include <vector>

#include "steam/steam_api.h"

namespace greenworks {

class FileShareWorker : public SteamCallbackAsyncWorker {
public:
  FileShareWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, const std::string &file_path);
  void OnFileShareCompleted(RemoteStorageFileShareResult_t *result, bool io_failure);

  void Execute() override;
  void HandleOKCallback() override;

private:
  const std::string file_path_;
  UGCHandle_t share_file_handle_;
  CCallResult<FileShareWorker, RemoteStorageFileShareResult_t> call_result_;
};

struct WorkshopFileProperties {
  static constexpr int MAX_TAGS = 100;
  std::string file_path;
  std::string image_path;
  std::string title;
  std::string description;

  std::vector<std::string> tags_scratch;
  const char *tags[MAX_TAGS];
};

class PublishWorkshopFileWorker : public SteamCallbackAsyncWorker {
public:
  PublishWorkshopFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, uint32 app_id, const WorkshopFileProperties &properties);
  void OnFilePublishCompleted(RemoteStoragePublishFileResult_t *result, bool io_failure);

  void Execute() override;
  void HandleOKCallback() override;

private:
  uint32 app_id_;
  WorkshopFileProperties properties_;

  PublishedFileId_t publish_file_id_;
  CCallResult<PublishWorkshopFileWorker, RemoteStoragePublishFileResult_t> call_result_;
};

class UpdatePublishedWorkshopFileWorker : public SteamCallbackAsyncWorker {
public:
  UpdatePublishedWorkshopFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t published_file_id,
                                    const WorkshopFileProperties &properties);
  void OnCommitPublishedFileUpdateCompleted(RemoteStorageUpdatePublishedFileResult_t *result, bool io_failure);

  void Execute() override;

private:
  PublishedFileId_t published_file_id_;
  WorkshopFileProperties properties_;

  CCallResult<UpdatePublishedWorkshopFileWorker, RemoteStorageUpdatePublishedFileResult_t> update_published_file_call_result_;
};

// A base worker class for querying (user/all) ugc.
class QueryUGCWorker : public SteamCallbackAsyncWorker {
public:
  QueryUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback);
  void OnUGCQueryCompleted(SteamUGCQueryCompleted_t *result, bool io_failure);

  void HandleOKCallback() override;
  void HandleErrorCallback() override;

protected:
  std::vector<SteamUGCDetails_t> ugc_items_;
  std::map<PublishedFileId_t, PublishedFileId_t *> child_map_;
  std::map<PublishedFileId_t, std::string *> key_value_tags_keys_;
  std::map<PublishedFileId_t, std::string *> key_value_tags_values_;
  std::map<PublishedFileId_t, uint32> num_tags_;
  std::map<PublishedFileId_t, std::string *> tags_;
  std::map<PublishedFileId_t, std::string *> tags_display_names_;
  std::map<PublishedFileId_t, std::string *> additional_preview_urls_;
  std::map<PublishedFileId_t, EItemPreviewType *> additional_preview_types_;
  std::map<PublishedFileId_t, std::string> metadata_;
  std::map<PublishedFileId_t, std::string> preview_urls_;
  uint32 total_matching_results_;
  uint32 num_results_returned_;
  CCallResult<QueryUGCWorker, SteamUGCQueryCompleted_t> ugc_query_call_result_;
};

// A base worker class for querying ugc details.
class QueryUGCDetailsWorker : public QueryUGCWorker {
public:
  QueryUGCDetailsWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t *id_list, uint32 num_ids);
  void Execute() override;

  void HandleOKCallback() override;
  void HandleErrorCallback() override;

protected:
  PublishedFileId_t *item_ids_;
  uint32 num_ids_;
};

class QueryUnknownUGCWorker : public QueryUGCWorker {
public:
  QueryUnknownUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, EUGCMatchingUGCType ugc_matching_type, uint32 app_id, uint32 page_num);

protected:
  EUGCMatchingUGCType ugc_matching_type_;
  uint32 app_id_;
  uint32 page_num_;
};

class QueryAllUGCWorker : public QueryUnknownUGCWorker {
public:
  QueryAllUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, EUGCMatchingUGCType ugc_matching_type, EUGCQuery ugc_query_type,
                    uint32 app_id, uint32 page_num, std::string required_tag);
  void Execute() override;

private:
  EUGCQuery ugc_query_type_;
  std::string required_tag_;
};

class QueryUserUGCWorker : public QueryUnknownUGCWorker {
public:
  QueryUserUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, EUGCMatchingUGCType ugc_matching_type, EUserUGCList ugc_list,
                     EUserUGCListSortOrder ugc_list_sort_order, uint32 app_id, uint32 page_num, std::string required_tag);
  void Execute() override;

private:
  EUserUGCList ugc_list_;
  std::string required_tag_;
  EUserUGCListSortOrder ugc_list_sort_order_;
};

class DownloadItemWorker : public SteamCallbackAsyncWorker {
public:
  DownloadItemWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t published_file_id);
  void HandleOKCallback() override;
  void Execute() override;

private:
  PublishedFileId_t published_file_id_;
  EResult result_;
  STEAM_CALLBACK(DownloadItemWorker, OnDownloadCompleted, DownloadItemResult_t, m_CallbackDownloadCompleted);
};

class DeleteItemWorker : public SteamCallbackAsyncWorker {
public:
  DeleteItemWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t published_file_id);
  void HandleOKCallback() override;
  void Execute() override;

private:
  PublishedFileId_t published_file_id_;
  EResult result_;
  STEAM_CALLBACK(DeleteItemWorker, OnDeleteCompleted, DeleteItemResult_t, m_CallbackDeleteCompleted);
};

class SynchronizeItemsWorker : public SteamCallbackAsyncWorker {
public:
  SynchronizeItemsWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, const std::string &download_dir, uint32 app_id, uint32 page_num);

  void OnUGCQueryCompleted(SteamUGCQueryCompleted_t *result, bool io_failure);
  void OnDownloadCompleted(RemoteStorageDownloadUGCResult_t *result, bool io_failure);

  void Execute() override;
  void HandleOKCallback() override;
  void HandleErrorCallback() override;

private:
  size_t current_download_items_pos_;
  std::string download_dir_;
  std::vector<SteamUGCDetails_t> ugc_items_;
  std::vector<UGCHandle_t> download_ugc_items_handle_;
  uint32 app_id_;
  uint32 page_num_;
  std::map<PublishedFileId_t, PublishedFileId_t *> child_map_;
  std::map<PublishedFileId_t, std::string *> key_value_tags_keys_;
  std::map<PublishedFileId_t, std::string *> key_value_tags_values_;
  std::map<PublishedFileId_t, uint32> num_tags_;
  std::map<PublishedFileId_t, std::string *> tags_;
  std::map<PublishedFileId_t, std::string *> tags_display_names_;
  std::map<PublishedFileId_t, std::string *> additional_preview_urls_;
  std::map<PublishedFileId_t, EItemPreviewType *> additional_preview_types_;
  std::map<PublishedFileId_t, std::string> metadata_;
  CCallResult<SynchronizeItemsWorker, RemoteStorageDownloadUGCResult_t> download_call_result_;
  CCallResult<SynchronizeItemsWorker, SteamUGCQueryCompleted_t> ugc_query_call_result_;
};

class UnsubscribePublishedFileWorker : public SteamCallbackAsyncWorker {
public:
  UnsubscribePublishedFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t unsubscribe_file_id);
  void OnUnsubscribeCompleted(RemoteStorageUnsubscribePublishedFileResult_t *result, bool io_failure);
  void HandleOKCallback() override;
  void Execute() override;

private:
  PublishedFileId_t unsubscribe_file_id_;
  EResult result_;
  CCallResult<UnsubscribePublishedFileWorker, RemoteStorageUnsubscribePublishedFileResult_t> call_result_;
};

class SubscribePublishedFileWorker : public SteamCallbackAsyncWorker {
public:
  SubscribePublishedFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t subscribe_file_id);
  void OnSubscribeCompleted(RemoteStorageSubscribePublishedFileResult_t *result, bool io_failure);
  void HandleOKCallback() override;
  void Execute() override;

private:
  PublishedFileId_t subscribe_file_id_;
  EResult result_;
  CCallResult<SubscribePublishedFileWorker, RemoteStorageSubscribePublishedFileResult_t> call_result_;
};
} // namespace greenworks

#endif // SRC_GREENWORKS_WORKSHOP_WORKERS_H_
