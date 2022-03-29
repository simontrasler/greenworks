// Copyright (c) 2014 Greenheart Games Pty. Ltd. All rights reserved.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "greenworks_workshop_workers.h"

#include <algorithm>
#include <map>

#include "nan.h"
#include "steam/steam_api.h"
#include "v8.h"

#include "greenworks_utils.h"

namespace {

char *PreviewTypeToString(EItemPreviewType type) {
  switch (type) {
  case EItemPreviewType::k_EItemPreviewType_Image:
    return "image";
  case EItemPreviewType::k_EItemPreviewType_YouTubeVideo:
    return "youtube";
  case EItemPreviewType::k_EItemPreviewType_Sketchfab:
    return "sketchfab";
  case EItemPreviewType::k_EItemPreviewType_EnvironmentMap_HorizontalCross:
    return "environmentmap-horizontalcross";
  case EItemPreviewType::k_EItemPreviewType_EnvironmentMap_LatLong:
    return "environmentmap-latlong";
  default:
    return "unknown";
  }
}

v8::Local<v8::Object> ConvertToJsObject(const SteamUGCDetails_t &item) {
  v8::Local<v8::Object> result = Nan::New<v8::Object>();

  Nan::Set(result, Nan::New("acceptedForUse").ToLocalChecked(), Nan::New(item.m_bAcceptedForUse));
  Nan::Set(result, Nan::New("banned").ToLocalChecked(), Nan::New(item.m_bBanned));
  Nan::Set(result, Nan::New("tagsTruncated").ToLocalChecked(), Nan::New(item.m_bTagsTruncated));
  Nan::Set(result, Nan::New("fileType").ToLocalChecked(), Nan::New(item.m_eFileType));
  Nan::Set(result, Nan::New("result").ToLocalChecked(), Nan::New(item.m_eResult));
  Nan::Set(result, Nan::New("visibility").ToLocalChecked(), Nan::New(item.m_eVisibility));
  Nan::Set(result, Nan::New("score").ToLocalChecked(), Nan::New(item.m_flScore));

  Nan::Set(result, Nan::New("file").ToLocalChecked(), Nan::New(utils::uint64ToString(item.m_hFile)).ToLocalChecked());
  Nan::Set(result, Nan::New("fileName").ToLocalChecked(), Nan::New(item.m_pchFileName).ToLocalChecked());
  Nan::Set(result, Nan::New("fileSize").ToLocalChecked(), Nan::New(item.m_nFileSize));

  Nan::Set(result, Nan::New("previewFile").ToLocalChecked(), Nan::New(utils::uint64ToString(item.m_hPreviewFile)).ToLocalChecked());
  Nan::Set(result, Nan::New("previewFileSize").ToLocalChecked(), Nan::New(item.m_nPreviewFileSize));

  Nan::Set(result, Nan::New("steamIDOwner").ToLocalChecked(), Nan::New(utils::uint64ToString(item.m_ulSteamIDOwner)).ToLocalChecked());
  Nan::Set(result, Nan::New("consumerAppID").ToLocalChecked(), Nan::New(item.m_nConsumerAppID));
  Nan::Set(result, Nan::New("creatorAppID").ToLocalChecked(), Nan::New(item.m_nCreatorAppID));
  Nan::Set(result, Nan::New("publishedFileId").ToLocalChecked(), Nan::New(utils::uint64ToString(item.m_nPublishedFileId)).ToLocalChecked());

  Nan::Set(result, Nan::New("title").ToLocalChecked(), Nan::New(item.m_rgchTitle).ToLocalChecked());
  Nan::Set(result, Nan::New("description").ToLocalChecked(), Nan::New(item.m_rgchDescription).ToLocalChecked());
  Nan::Set(result, Nan::New("URL").ToLocalChecked(), Nan::New(item.m_rgchURL).ToLocalChecked());

  Nan::Set(result, Nan::New("timeAddedToUserList").ToLocalChecked(), Nan::New(item.m_rtimeAddedToUserList));
  Nan::Set(result, Nan::New("timeCreated").ToLocalChecked(), Nan::New(item.m_rtimeCreated));
  Nan::Set(result, Nan::New("timeUpdated").ToLocalChecked(), Nan::New(item.m_rtimeUpdated));
  Nan::Set(result, Nan::New("votesDown").ToLocalChecked(), Nan::New(item.m_unVotesDown));
  Nan::Set(result, Nan::New("votesUp").ToLocalChecked(), Nan::New(item.m_unVotesUp));
  Nan::Set(result, Nan::New("numChildren").ToLocalChecked(), Nan::New(item.m_unNumChildren));

  return result;
}

inline std::string GetAbsoluteFilePath(const std::string &file_path, const std::string &download_dir) {
  std::string file_name = file_path.substr(file_path.find_last_of("/\\") + 1);
  return download_dir + "/" + file_name;
}

} // namespace

namespace greenworks {
FileShareWorker::FileShareWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, const std::string &file_path)
    : SteamCallbackAsyncWorker(success_callback, error_callback), file_path_(file_path) {}

void FileShareWorker::Execute() {
  // Ignore empty path.
  if (file_path_.empty())
    return;

  std::string file_name = utils::GetFileNameFromPath(file_path_);
  SteamAPICall_t share_result = SteamRemoteStorage()->FileShare(file_name.c_str());
  call_result_.Set(share_result, this, &FileShareWorker::OnFileShareCompleted);

  // Wait for FileShare callback result.
  WaitForCompleted();
}
void FileShareWorker::OnFileShareCompleted(RemoteStorageFileShareResult_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on sharing file: Steam API IO Failure");
  } else if (result->m_eResult == k_EResultOK) {
    share_file_handle_ = result->m_hFile;
  } else {
    SetErrorMessage("Error on sharing file on Steam cloud.");
  }
  is_completed_ = true;
}
void FileShareWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Value> argv[] = {Nan::New(utils::uint64ToString(share_file_handle_)).ToLocalChecked()};
  Nan::AsyncResource resource("greenworks:FileShareWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}

PublishWorkshopFileWorker::PublishWorkshopFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, uint32 app_id,
                                                     const WorkshopFileProperties &properties)
    : SteamCallbackAsyncWorker(success_callback, error_callback), app_id_(app_id), properties_(properties) {}

void PublishWorkshopFileWorker::Execute() {
  SteamParamStringArray_t tags;
  tags.m_nNumStrings = properties_.tags_scratch.size();
  tags.m_ppStrings = reinterpret_cast<const char **>(&properties_.tags);

  std::string file_name = utils::GetFileNameFromPath(properties_.file_path);
  std::string image_name = utils::GetFileNameFromPath(properties_.image_path);
  SteamAPICall_t publish_result =
      SteamRemoteStorage()->PublishWorkshopFile(file_name.c_str(), image_name.empty() ? nullptr : image_name.c_str(), app_id_, properties_.title.c_str(),
                                                properties_.description.empty() ? nullptr : properties_.description.c_str(),
                                                k_ERemoteStoragePublishedFileVisibilityPublic, &tags, k_EWorkshopFileTypeCommunity);

  call_result_.Set(publish_result, this, &PublishWorkshopFileWorker::OnFilePublishCompleted);

  // Wait for FileShare callback result.
  WaitForCompleted();
}
void PublishWorkshopFileWorker::OnFilePublishCompleted(RemoteStoragePublishFileResult_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on publishing workshop file: Steam API IO Failure");
  } else if (result->m_eResult == k_EResultOK) {
    publish_file_id_ = result->m_nPublishedFileId;
  } else {
    SetErrorMessage("Error on publishing workshop file.");
  }
  is_completed_ = true;
}
void PublishWorkshopFileWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Value> argv[] = {Nan::New(utils::uint64ToString(publish_file_id_)).ToLocalChecked()};
  Nan::AsyncResource resource("greenworks:PublishWorkshopFileWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}

UpdatePublishedWorkshopFileWorker::UpdatePublishedWorkshopFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback,
                                                                     PublishedFileId_t published_file_id, const WorkshopFileProperties &properties)
    : SteamCallbackAsyncWorker(success_callback, error_callback), published_file_id_(published_file_id), properties_(properties) {}

void UpdatePublishedWorkshopFileWorker::Execute() {
  PublishedFileUpdateHandle_t update_handle = SteamRemoteStorage()->CreatePublishedFileUpdateRequest(published_file_id_);

  const std::string file_name = utils::GetFileNameFromPath(properties_.file_path);
  const std::string image_name = utils::GetFileNameFromPath(properties_.image_path);
  if (!file_name.empty())
    SteamRemoteStorage()->UpdatePublishedFileFile(update_handle, file_name.c_str());
  if (!image_name.empty())
    SteamRemoteStorage()->UpdatePublishedFilePreviewFile(update_handle, image_name.c_str());
  if (!properties_.title.empty())
    SteamRemoteStorage()->UpdatePublishedFileTitle(update_handle, properties_.title.c_str());
  if (!properties_.description.empty())
    SteamRemoteStorage()->UpdatePublishedFileDescription(update_handle, properties_.description.c_str());
  if (!properties_.tags_scratch.empty()) {
    SteamParamStringArray_t tags;
    if (properties_.tags_scratch.size() == 1 && properties_.tags_scratch.front().empty()) { // clean the tag.
      tags.m_nNumStrings = 0;
      tags.m_ppStrings = nullptr;
    } else {
      tags.m_nNumStrings = properties_.tags_scratch.size();
      tags.m_ppStrings = reinterpret_cast<const char **>(&properties_.tags);
    }
    SteamRemoteStorage()->UpdatePublishedFileTags(update_handle, &tags);
  }
  SteamAPICall_t commit_update_result = SteamRemoteStorage()->CommitPublishedFileUpdate(update_handle);
  update_published_file_call_result_.Set(commit_update_result, this, &UpdatePublishedWorkshopFileWorker::OnCommitPublishedFileUpdateCompleted);

  // Wait for published workshop file updated.
  WaitForCompleted();
}
void UpdatePublishedWorkshopFileWorker::OnCommitPublishedFileUpdateCompleted(RemoteStorageUpdatePublishedFileResult_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on committing published file update: Steam API IO Failure");
  } else if (result->m_eResult == k_EResultOK) {
  } else {
    SetErrorMessage("Error on getting published file details.");
  }
  is_completed_ = true;
}

QueryUGCWorker::QueryUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback) : SteamCallbackAsyncWorker(success_callback, error_callback) {}

void QueryUGCWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Array> items = Nan::New<v8::Array>(static_cast<int>(ugc_items_.size()));
  for (size_t i = 0; i < ugc_items_.size(); ++i) {
    v8::Local<v8::Object> item = ConvertToJsObject(ugc_items_[i]);
    PublishedFileId_t workshop_id = ugc_items_[i].m_nPublishedFileId;
    // Write out child data
    int numChildren = ugc_items_[i].m_unNumChildren;
    if (numChildren > 0 && child_map_.count(workshop_id) > 0) {
      PublishedFileId_t *child_list = child_map_[workshop_id];
      v8::Local<v8::Array> child_items = Nan::New<v8::Array>(numChildren);
      for (size_t j = 0; j < numChildren; j++) {
        PublishedFileId_t child_item = child_list[j];
        Nan::Set(child_items, j, Nan::New(utils::uint64ToString(child_item)).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("children").ToLocalChecked(), child_items);
      delete[] child_list;
    }
    // Get metadata
    if (metadata_.size() > 0 && metadata_.count(workshop_id) > 0) {
      std::string metadata = metadata_[workshop_id];
      Nan::Set(item, Nan::New("metadata").ToLocalChecked(), Nan::New(metadata).ToLocalChecked());
    }
    // Write out Key Value tags
    if (key_value_tags_keys_.size() > 0 && key_value_tags_keys_.count(workshop_id) > 0) {
      std::string *keys = key_value_tags_keys_[workshop_id];
      std::string *tags = key_value_tags_values_[workshop_id];
      int numChildren = sizeof(keys) / sizeof(std::string);
      v8::Local<v8::Array> v8_tags = Nan::New<v8::Array>(numChildren);
      v8::Local<v8::Array> v8_keys = Nan::New<v8::Array>(numChildren);
      for (size_t j = 0; j < numChildren; j++) {
        std::string tag = tags[j];
        Nan::Set(v8_tags, j, Nan::New(tag).ToLocalChecked());
        std::string key = keys[j];
        Nan::Set(v8_keys, j, Nan::New(key).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("keyValueTagsKeys").ToLocalChecked(), v8_keys);
      Nan::Set(item, Nan::New("keyValueTagsValues").ToLocalChecked(), v8_tags);
      delete[] tags;
      delete[] keys;
    }
    // Write out tags
    uint32 num_tags = num_tags_[workshop_id];
    if (num_tags > 0) {
      std::string *tags = tags_[workshop_id];
      std::string *display_names = tags_display_names_[workshop_id];
      v8::Local<v8::Array> v8_tags = Nan::New<v8::Array>(num_tags);
      v8::Local<v8::Array> v8_tags_display_names = Nan::New<v8::Array>(num_tags);
      for (size_t j = 0; j < num_tags; j++) {
        std::string tag = tags[j];
        Nan::Set(v8_tags, j, Nan::New(tag).ToLocalChecked());
        std::string key = display_names[j];
        Nan::Set(v8_tags_display_names, j, Nan::New(key).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("tags").ToLocalChecked(), v8_tags);
      Nan::Set(item, Nan::New("tagsDisplayNames").ToLocalChecked(), v8_tags_display_names);
      delete[] tags;
      delete[] display_names;
    }
    // Write out additional previews
    if (additional_preview_types_.size() > 0 && additional_preview_types_.count(workshop_id) > 0) {
      std::string *urls = additional_preview_urls_[workshop_id];
      EItemPreviewType *types = additional_preview_types_[workshop_id];
      int numChildren = sizeof(urls) / sizeof(std::string);
      v8::Local<v8::Array> v8_urls = Nan::New<v8::Array>(numChildren);
      v8::Local<v8::Array> v8_types = Nan::New<v8::Array>(numChildren);
      for (size_t j = 0; j < numChildren; j++) {
        EItemPreviewType type = types[j];
        Nan::Set(v8_types, j, Nan::New(PreviewTypeToString(type)).ToLocalChecked());
        std::string url = urls[j];
        Nan::Set(v8_urls, j, Nan::New(url).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("additionalPreviewURLs").ToLocalChecked(), v8_urls);
      Nan::Set(item, Nan::New("additionalPreviewTypes").ToLocalChecked(), v8_types);
      delete[] urls;
      delete[] types;
    }
    Nan::Set(items, i, item);
  }
  child_map_.clear();
  additional_preview_types_.clear();
  additional_preview_urls_.clear();
  tags_.clear();
  num_tags_.clear();
  tags_display_names_.clear();
  metadata_.clear();
  key_value_tags_keys_.clear();
  key_value_tags_values_.clear();

  v8::Local<v8::Object> result = Nan::New<v8::Object>();
	Nan::Set(result, Nan::New("items").ToLocalChecked(), items);
	Nan::Set(result, Nan::New("totalItems").ToLocalChecked(), Nan::New(total_matching_results_));
	Nan::Set(result, Nan::New("numReturned").ToLocalChecked(), Nan::New(num_results_returned_));

  v8::Local<v8::Value> argv[] = {result};
  Nan::AsyncResource resource("greenworks:QueryUGCWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}
void QueryUGCWorker::OnUGCQueryCompleted(SteamUGCQueryCompleted_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on querying all ugc: Steam API IO Failure");
  } else if (result->m_eResult == k_EResultOK) {
    uint32 count = result->m_unNumResultsReturned;
    total_matching_results_ = result->m_unTotalMatchingResults;
    num_results_returned_ = count;

    SteamUGCDetails_t item;
    for (uint32 i = 0; i < count; ++i) {
      SteamUGC()->GetQueryUGCResult(result->m_handle, i, &item);
      ugc_items_.push_back(item);

      // Get required items
      if (item.m_unNumChildren > 0) {
        PublishedFileId_t *pvecPublishedFileID = new PublishedFileId_t[item.m_unNumChildren];
        SteamUGC()->GetQueryUGCChildren(result->m_handle, i, pvecPublishedFileID, item.m_unNumChildren);
        child_map_[item.m_nPublishedFileId] = pvecPublishedFileID;
      }

      // Get metadata
      char *pchMetadata = new char[k_cchDeveloperMetadataMax];
      if (SteamUGC()->GetQueryUGCMetadata(result->m_handle, i, pchMetadata, k_cchDeveloperMetadataMax)) {
        metadata_[item.m_nPublishedFileId] = pchMetadata;
      } else {
        delete[] pchMetadata;
      }

      // Get Key Value tags
      uint32 maxTagSize = 64;
      uint32 numKeyValueTags = SteamUGC()->GetQueryUGCNumKeyValueTags(result->m_handle, i);
      if (numKeyValueTags > 0) {
        std::string *keys = new std::string[numKeyValueTags];
        std::string *values = new std::string[numKeyValueTags];
        for (uint32 j = 0; j < numKeyValueTags; j++) {
          char *key = new char[maxTagSize];
          char *value = new char[maxTagSize];
          if (SteamUGC()->GetQueryUGCKeyValueTag(result->m_handle, i, j, key, maxTagSize, value, maxTagSize)) {
            keys[j] = key;
            values[j] = value;
          } else {
            keys[j] = "";
            values[j] = "";
            delete[] key;
            delete[] value;
          }
        }
        key_value_tags_keys_[item.m_nPublishedFileId] = keys;
        key_value_tags_values_[item.m_nPublishedFileId] = values;
      }

      // Get tags
      uint32 numTags = SteamUGC()->GetQueryUGCNumTags(result->m_handle, i);
      num_tags_[item.m_nPublishedFileId] = numTags;
      if (numTags > 0) {
        std::string *tags = new std::string[numTags];
        std::string *tag_display_names = new std::string[numTags];
        for (uint32 j = 0; j < numTags; j++) {
          char *tag = new char[maxTagSize];
          if (SteamUGC()->GetQueryUGCTag(result->m_handle, i, j, tag, maxTagSize)) {
            tags[j] = tag;
          } else {
            tags[j] = "";
            delete[] tag;
          }

          char *displayTag = new char[maxTagSize];
          if (SteamUGC()->GetQueryUGCTagDisplayName(result->m_handle, i, j, displayTag, maxTagSize)) {
            tag_display_names[j] = displayTag;
          } else {
            tag_display_names[j] = "";
            delete[] displayTag;
          }
        }
        tags_[item.m_nPublishedFileId] = tags;
        tags_display_names_[item.m_nPublishedFileId] = tag_display_names;
      }

      // Get additional previews
      uint32 maxPreviewURLSize = 500;
      uint32 numPreviews = SteamUGC()->GetQueryUGCNumAdditionalPreviews(result->m_handle, i);
      if (numPreviews > 0) {
        std::string *previewURLs = new std::string[numPreviews];
        EItemPreviewType *previewTypes = new EItemPreviewType[numPreviews];
        for (uint32 j = 0; j < numPreviews; j++) {
          char *pchURL = new char[maxPreviewURLSize];
          EItemPreviewType *previewType;
          if (SteamUGC()->GetQueryUGCAdditionalPreview(result->m_handle, i, j, pchURL, maxPreviewURLSize, nullptr, 0, previewType)) {
            previewURLs[j] = pchURL;
            previewTypes[j] = *previewType;
          } else {
            delete[] pchURL;
            delete previewType;
            previewURLs[j] = "";
            previewTypes[j] = EItemPreviewType::k_EItemPreviewType_Image;
          }
        }
      }
    }
    SteamUGC()->ReleaseQueryUGCRequest(result->m_handle);
  } else {
    SetErrorMessage("Error on querying ugc.");
  }
  is_completed_ = true;
}
void QueryUGCWorker::HandleErrorCallback() {
  if (child_map_.size() > 0) {
    for (std::map<PublishedFileId_t, PublishedFileId_t *>::iterator it = child_map_.begin(); it != child_map_.end(); it++) {
      PublishedFileId_t *child_array = it->second;
      delete[] child_array;
    }
    child_map_.clear();
  }
  if (key_value_tags_keys_.size() > 0) {
    for (std::map<PublishedFileId_t, std::string *>::iterator it = key_value_tags_keys_.begin(); it != key_value_tags_keys_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    key_value_tags_keys_.clear();
    for (std::map<PublishedFileId_t, std::string *>::iterator it = key_value_tags_values_.begin(); it != key_value_tags_values_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    key_value_tags_values_.clear();
  }
  if (tags_.size() > 0) {
    for (std::map<PublishedFileId_t, std::string *>::iterator it = tags_.begin(); it != tags_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    tags_.clear();
    for (std::map<PublishedFileId_t, std::string *>::iterator it = tags_display_names_.begin(); it != tags_display_names_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    tags_display_names_.clear();
  }
  if (additional_preview_types_.size() > 0) {
    for (std::map<PublishedFileId_t, EItemPreviewType *>::iterator it = additional_preview_types_.begin(); it != additional_preview_types_.end(); it++) {
      EItemPreviewType *type_array = it->second;
      delete[] type_array;
    }
    additional_preview_types_.clear();
    for (std::map<PublishedFileId_t, std::string *>::iterator it = additional_preview_urls_.begin(); it != additional_preview_urls_.end(); it++) {
      std::string *url_array = it->second;
      delete[] url_array;
    }
    additional_preview_urls_.clear();
  }
  num_tags_.clear();
  metadata_.clear();

  if (!error_callback_)
    return;
  Nan::HandleScope scope;
  v8::Local<v8::Value> argv[] = {Nan::New(ErrorMessage()).ToLocalChecked()};
  Nan::AsyncResource resource("greenworks:QueryUGCWorker.HandleErrorCallback");
  error_callback_->Call(1, argv, &resource);
}

QueryUGCDetailsWorker::QueryUGCDetailsWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t *id_list, uint32 num_ids)
    : QueryUGCWorker(success_callback, error_callback), item_ids_(id_list), num_ids_(num_ids) {}
void QueryUGCDetailsWorker::Execute() {
  UGCQueryHandle_t ugc_handle = SteamUGC()->CreateQueryUGCDetailsRequest(item_ids_, num_ids_);
  SteamUGC()->SetReturnLongDescription(ugc_handle, true);
  SteamUGC()->SetReturnChildren(ugc_handle, true);
  SteamUGC()->SetReturnAdditionalPreviews(ugc_handle, true);
  SteamUGC()->SetReturnKeyValueTags(ugc_handle, true);
  SteamUGC()->SetReturnMetadata(ugc_handle, true);
  SteamAPICall_t ugc_query_result = SteamUGC()->SendQueryUGCRequest(ugc_handle);
  ugc_query_call_result_.Set(ugc_query_result, this, &QueryUGCDetailsWorker::OnUGCQueryCompleted);

  // Wait for query all ugc completed.
  WaitForCompleted();
}
void QueryUGCDetailsWorker::HandleOKCallback() {
  delete[] item_ids_;
  QueryUGCWorker::HandleOKCallback();
}
void QueryUGCDetailsWorker::HandleErrorCallback() {
  delete[] item_ids_;
  QueryUGCWorker::HandleErrorCallback();
}

QueryUnknownUGCWorker::QueryUnknownUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, EUGCMatchingUGCType ugc_matching_type,
                                             uint32 app_id, uint32 page_num)
    : QueryUGCWorker(success_callback, error_callback), ugc_matching_type_(ugc_matching_type), app_id_(app_id), page_num_(page_num) {}

QueryAllUGCWorker::QueryAllUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, EUGCMatchingUGCType ugc_matching_type,
                                     EUGCQuery ugc_query_type, uint32 app_id, uint32 page_num, std::string required_tag)
    : QueryUnknownUGCWorker(success_callback, error_callback, ugc_matching_type, app_id, page_num), ugc_query_type_(ugc_query_type),
      required_tag_(required_tag) {}

void QueryAllUGCWorker::Execute() {
  uint32 invalid_app_id = 0;
  // Set "creator_app_id" parameter to an invalid id to make Steam API return
  // all ugc items, otherwise the API won't get any results in some cases.
  UGCQueryHandle_t ugc_handle = SteamUGC()->CreateQueryAllUGCRequest(ugc_query_type_, ugc_matching_type_, /*creator_app_id=*/invalid_app_id,
                                                                     /*consumer_app_id=*/app_id_, page_num_);
  SteamUGC()->SetReturnLongDescription(ugc_handle, true);
  SteamUGC()->SetReturnChildren(ugc_handle, true);
  SteamUGC()->SetReturnAdditionalPreviews(ugc_handle, true);
  SteamUGC()->SetReturnKeyValueTags(ugc_handle, true);
  SteamUGC()->SetReturnMetadata(ugc_handle, true);
  if (required_tag_.length() > 0) {
    const char *tag = required_tag_.c_str();
    SteamUGC()->AddRequiredTag(ugc_handle, tag);
  }
  SteamAPICall_t ugc_query_result = SteamUGC()->SendQueryUGCRequest(ugc_handle);
  ugc_query_call_result_.Set(ugc_query_result, this, &QueryAllUGCWorker::OnUGCQueryCompleted);

  // Wait for query all ugc completed.
  WaitForCompleted();
}

QueryUserUGCWorker::QueryUserUGCWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, EUGCMatchingUGCType ugc_matching_type,
                                       EUserUGCList ugc_list, EUserUGCListSortOrder ugc_list_sort_order, uint32 app_id, uint32 page_num,
                                       std::string required_tag)
    : QueryUnknownUGCWorker(success_callback, error_callback, ugc_matching_type, app_id, page_num), ugc_list_(ugc_list),
      ugc_list_sort_order_(ugc_list_sort_order), required_tag_(required_tag) {}
void QueryUserUGCWorker::Execute() {
  UGCQueryHandle_t ugc_handle = SteamUGC()->CreateQueryUserUGCRequest(SteamUser()->GetSteamID().GetAccountID(), ugc_list_, ugc_matching_type_,
                                                                      ugc_list_sort_order_, app_id_, app_id_, page_num_);
  SteamUGC()->SetReturnLongDescription(ugc_handle, true);
  SteamUGC()->SetReturnChildren(ugc_handle, true);
  SteamUGC()->SetReturnAdditionalPreviews(ugc_handle, true);
  SteamUGC()->SetReturnKeyValueTags(ugc_handle, true);
  SteamUGC()->SetReturnMetadata(ugc_handle, true);
  if (required_tag_.length() > 0) {
    const char *tag = required_tag_.c_str();
    SteamUGC()->AddRequiredTag(ugc_handle, tag);
  }
  SteamAPICall_t ugc_query_result = SteamUGC()->SendQueryUGCRequest(ugc_handle);
  ugc_query_call_result_.Set(ugc_query_result, this, &QueryUserUGCWorker::OnUGCQueryCompleted);

  // Wait for query all ugc completed.
  WaitForCompleted();
}

DownloadItemWorker::DownloadItemWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t published_file_id)
    : SteamCallbackAsyncWorker(success_callback, error_callback), published_file_id_(published_file_id),
      m_CallbackDownloadCompleted(this, &DownloadItemWorker::OnDownloadCompleted) {}
void DownloadItemWorker::Execute() {
  if (SteamUGC()->DownloadItem(published_file_id_, false)) {
    // call_result_.Set(nullptr, this, &DownloadItemWorker::OnDownloadCompleted);
    // currently nonfunctional - don't know how to setup this callback properly
  } else {
    is_completed_ = true;
  }

  // Wait for downloading file completed.
  WaitForCompleted();
}
void DownloadItemWorker::OnDownloadCompleted(DownloadItemResult_t *result) {
  if (result->m_unAppID == SteamUtils()->GetAppID() && result->m_nPublishedFileId == published_file_id_) {
    result_ = result->m_eResult;
    is_completed_ = true;
  }
}
void DownloadItemWorker::HandleOKCallback() {
  Nan::HandleScope scope;
  v8::Local<v8::Value> argv[] = {Nan::New(result_)};
  Nan::AsyncResource resource("greenworks:DownloadItemWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}

SynchronizeItemsWorker::SynchronizeItemsWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, const std::string &download_dir, uint32 app_id,
                                               uint32 page_num)
    : SteamCallbackAsyncWorker(success_callback, error_callback), current_download_items_pos_(0), download_dir_(download_dir), app_id_(app_id),
      page_num_(page_num) {}

void SynchronizeItemsWorker::Execute() {
  UGCQueryHandle_t ugc_handle =
      SteamUGC()->CreateQueryUserUGCRequest(SteamUser()->GetSteamID().GetAccountID(), k_EUserUGCList_Subscribed, k_EUGCMatchingUGCType_Items_ReadyToUse,
                                            k_EUserUGCListSortOrder_SubscriptionDateDesc, app_id_, app_id_, page_num_);
  SteamUGC()->SetReturnLongDescription(ugc_handle, true);
  SteamUGC()->SetReturnChildren(ugc_handle, true);
  SteamUGC()->SetReturnAdditionalPreviews(ugc_handle, true);
  SteamUGC()->SetReturnKeyValueTags(ugc_handle, true);
  SteamUGC()->SetReturnMetadata(ugc_handle, true);
  SteamAPICall_t ugc_query_result = SteamUGC()->SendQueryUGCRequest(ugc_handle);
  ugc_query_call_result_.Set(ugc_query_result, this, &SynchronizeItemsWorker::OnUGCQueryCompleted);

  // Wait for synchronization completed.
  WaitForCompleted();
}

void SynchronizeItemsWorker::OnUGCQueryCompleted(SteamUGCQueryCompleted_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on querying all ugc: Steam API IO Failure");
  } else if (result->m_eResult == k_EResultOK) {
    SteamUGCDetails_t item;
    for (uint32 i = 0; i < result->m_unNumResultsReturned; ++i) {
      SteamUGC()->GetQueryUGCResult(result->m_handle, i, &item);
      std::string target_path = GetAbsoluteFilePath(item.m_pchFileName, download_dir_);
      int64 file_update_time = utils::GetFileLastUpdatedTime(target_path.c_str());
      ugc_items_.push_back(item);
      // If the file is not existed or last update time is not equal to Steam,
      // download it.
      if (file_update_time == -1 || file_update_time != item.m_rtimeUpdated)
        download_ugc_items_handle_.push_back(item.m_hFile);

      // Get required items
      if (item.m_unNumChildren > 0) {
        PublishedFileId_t *pvecPublishedFileID = new PublishedFileId_t[item.m_unNumChildren];
        SteamUGC()->GetQueryUGCChildren(result->m_handle, i, pvecPublishedFileID, item.m_unNumChildren);
        child_map_[item.m_nPublishedFileId] = pvecPublishedFileID;
      }
      // Get metadata
      char *pchMetadata = new char[k_cchDeveloperMetadataMax];
      if (SteamUGC()->GetQueryUGCMetadata(result->m_handle, i, pchMetadata, k_cchDeveloperMetadataMax)) {
        metadata_[item.m_nPublishedFileId] = pchMetadata;
      } else {
        delete[] pchMetadata;
      }

      // Get Key Value tags
      uint32 maxTagSize = 64;
      uint32 numKeyValueTags = SteamUGC()->GetQueryUGCNumKeyValueTags(result->m_handle, i);
      if (numKeyValueTags > 0) {
        std::string *keys = new std::string[numKeyValueTags];
        std::string *values = new std::string[numKeyValueTags];
        for (uint32 j = 0; j < numKeyValueTags; j++) {
          char *key = new char[maxTagSize];
          char *value = new char[maxTagSize];
          if (SteamUGC()->GetQueryUGCKeyValueTag(result->m_handle, i, j, key, maxTagSize, value, maxTagSize)) {
            keys[j] = key;
            values[j] = value;
          } else {
            keys[j] = "";
            values[j] = "";
            delete[] key;
            delete[] value;
          }
        }
        key_value_tags_keys_[item.m_nPublishedFileId] = keys;
        key_value_tags_values_[item.m_nPublishedFileId] = values;
      }

      // Get tags
      uint32 numTags = SteamUGC()->GetQueryUGCNumTags(result->m_handle, i);
      if (numTags > 0) {
        std::string *tags = new std::string[numTags];
        std::string *tag_display_names = new std::string[numTags];
        for (uint32 j = 0; j < numTags; j++) {
          char *tag = new char[maxTagSize];
          if (SteamUGC()->GetQueryUGCTag(result->m_handle, i, j, tag, maxTagSize)) {
            tags[j] = tag;
          } else {
            tags[j] = "";
            delete[] tag;
          }

          char *displayTag = new char[maxTagSize];
          if (SteamUGC()->GetQueryUGCTagDisplayName(result->m_handle, i, j, displayTag, maxTagSize)) {
            tag_display_names[j] = displayTag;
          } else {
            tag_display_names[j] = "";
            delete[] displayTag;
          }
        }
        tags_[item.m_nPublishedFileId] = tags;
        tags_display_names_[item.m_nPublishedFileId] = tag_display_names;
      }
      num_tags_[item.m_nPublishedFileId] = numTags;

      // Get additional previews
      uint32 maxPreviewURLSize = 500;
      uint32 numPreviews = SteamUGC()->GetQueryUGCNumAdditionalPreviews(result->m_handle, i);
      if (numPreviews > 0) {
        std::string *previewURLs = new std::string[numPreviews];
        EItemPreviewType *previewTypes = new EItemPreviewType[numPreviews];
        for (uint32 j = 0; j < numPreviews; j++) {
          char *pchURL = new char[maxPreviewURLSize];
          EItemPreviewType *previewType;
          if (SteamUGC()->GetQueryUGCAdditionalPreview(result->m_handle, i, j, pchURL, maxPreviewURLSize, nullptr, 0, previewType)) {
            previewURLs[j] = pchURL;
            previewTypes[j] = *previewType;
          } else {
            delete[] pchURL;
            delete previewType;
            previewURLs[j] = "";
            previewTypes[j] = EItemPreviewType::k_EItemPreviewType_Image;
          }
        }
      }
    }

    // Start download the file.
    if (download_ugc_items_handle_.size() > 0) {
      SteamAPICall_t download_item_result = SteamRemoteStorage()->UGCDownload(download_ugc_items_handle_[current_download_items_pos_], 0);
      download_call_result_.Set(download_item_result, this, &SynchronizeItemsWorker::OnDownloadCompleted);
      SteamUGC()->ReleaseQueryUGCRequest(result->m_handle);
      return;
    }
  } else {
    SetErrorMessage("Error on querying ugc.");
  }
  is_completed_ = true;
}

void SynchronizeItemsWorker::OnDownloadCompleted(RemoteStorageDownloadUGCResult_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on downloading file: Steam API IO Failure");
  } else if (result->m_eResult == k_EResultOK) {
    std::string target_path = GetAbsoluteFilePath(result->m_pchFileName, download_dir_);

    int file_size_in_bytes = result->m_nSizeInBytes;
    auto *content = new char[file_size_in_bytes];

    SteamRemoteStorage()->UGCRead(result->m_hFile, content, file_size_in_bytes, 0, k_EUGCRead_Close);
    bool is_save_success = utils::WriteFile(target_path, content, file_size_in_bytes);
    delete[] content;

    if (!is_save_success) {
      SetErrorMessage("Error on saving file on local machine.");
      is_completed_ = true;
      return;
    }

    int64 file_updated_time = ugc_items_[current_download_items_pos_].m_rtimeUpdated;
    if (!utils::UpdateFileLastUpdatedTime(target_path.c_str(), static_cast<time_t>(file_updated_time))) {
      SetErrorMessage("Error on update file time on local machine.");
      is_completed_ = true;
      return;
    }
    ++current_download_items_pos_;
    if (current_download_items_pos_ < download_ugc_items_handle_.size()) {
      SteamAPICall_t download_item_result = SteamRemoteStorage()->UGCDownload(download_ugc_items_handle_[current_download_items_pos_], 0);
      download_call_result_.Set(download_item_result, this, &SynchronizeItemsWorker::OnDownloadCompleted);
      return;
    }
  } else {
    SetErrorMessage("Error on downloading file.");
  }
  is_completed_ = true;
}

void SynchronizeItemsWorker::HandleErrorCallback() {
  if (child_map_.size() > 0) {
    for (std::map<PublishedFileId_t, PublishedFileId_t *>::iterator it = child_map_.begin(); it != child_map_.end(); it++) {
      PublishedFileId_t *child_array = it->second;
      delete[] child_array;
    }
    child_map_.clear();
  }
  if (key_value_tags_keys_.size() > 0) {
    for (std::map<PublishedFileId_t, std::string *>::iterator it = key_value_tags_keys_.begin(); it != key_value_tags_keys_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    key_value_tags_keys_.clear();
    for (std::map<PublishedFileId_t, std::string *>::iterator it = key_value_tags_values_.begin(); it != key_value_tags_values_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    key_value_tags_values_.clear();
  }
  if (tags_.size() > 0) {
    for (std::map<PublishedFileId_t, std::string *>::iterator it = tags_.begin(); it != tags_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    tags_.clear();
    for (std::map<PublishedFileId_t, std::string *>::iterator it = tags_display_names_.begin(); it != tags_display_names_.end(); it++) {
      std::string *tags_array = it->second;
      delete[] tags_array;
    }
    tags_display_names_.clear();
  }
  if (additional_preview_types_.size() > 0) {
    for (std::map<PublishedFileId_t, EItemPreviewType *>::iterator it = additional_preview_types_.begin(); it != additional_preview_types_.end(); it++) {
      EItemPreviewType *type_array = it->second;
      delete[] type_array;
    }
    additional_preview_types_.clear();
    for (std::map<PublishedFileId_t, std::string *>::iterator it = additional_preview_urls_.begin(); it != additional_preview_urls_.end(); it++) {
      std::string *url_array = it->second;
      delete[] url_array;
    }
    additional_preview_urls_.clear();
  }
  num_tags_.clear();
  metadata_.clear();

  if (!error_callback_)
    return;
  Nan::HandleScope scope;
  v8::Local<v8::Value> argv[] = {Nan::New(ErrorMessage()).ToLocalChecked()};
  Nan::AsyncResource resource("greenworks:SynchronizeItemsWorker.HandleErrorCallback");
  error_callback_->Call(1, argv, &resource);
}

void SynchronizeItemsWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Array> items = Nan::New<v8::Array>(static_cast<int>(ugc_items_.size()));
  for (size_t i = 0; i < ugc_items_.size(); ++i) {
    v8::Local<v8::Object> item = ConvertToJsObject(ugc_items_[i]);
    PublishedFileId_t workshop_id = ugc_items_[i].m_nPublishedFileId;
    // Write out child data
    int numChildren = ugc_items_[i].m_unNumChildren;
    if (numChildren > 0 && child_map_.count(workshop_id) > 0) {
      PublishedFileId_t *child_list = child_map_[workshop_id];
      v8::Local<v8::Array> child_items = Nan::New<v8::Array>(numChildren);
      for (size_t j = 0; j < numChildren; j++) {
        PublishedFileId_t child_item = child_list[j];
        Nan::Set(child_items, j, Nan::New(utils::uint64ToString(child_item)).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("children").ToLocalChecked(), child_items);
      delete[] child_list;
    }
    // Get metadata
    if (metadata_.size() > 0 && metadata_.count(workshop_id) > 0) {
      std::string metadata = metadata_[workshop_id];
      Nan::Set(item, Nan::New("metadata").ToLocalChecked(), Nan::New(metadata).ToLocalChecked());
    }
    // Write out Key Value tags
    if (key_value_tags_keys_.size() > 0 && key_value_tags_keys_.count(workshop_id) > 0) {
      std::string *keys = key_value_tags_keys_[workshop_id];
      std::string *tags = key_value_tags_values_[workshop_id];
      int numChildren = sizeof(keys) / sizeof(std::string);
      v8::Local<v8::Array> v8_tags = Nan::New<v8::Array>(numChildren);
      v8::Local<v8::Array> v8_keys = Nan::New<v8::Array>(numChildren);
      for (size_t j = 0; j < numChildren; j++) {
        std::string tag = tags[j];
        Nan::Set(v8_tags, j, Nan::New(tag).ToLocalChecked());
        std::string key = keys[j];
        Nan::Set(v8_keys, j, Nan::New(key).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("keyValueTagsKeys").ToLocalChecked(), v8_keys);
      Nan::Set(item, Nan::New("keyValueTagsValues").ToLocalChecked(), v8_tags);
      delete[] tags;
      delete[] keys;
    }
    // Write out tags
    uint32 num_tags = num_tags_[workshop_id];
    if (num_tags > 0) {
      std::string *tags = tags_[workshop_id];
      std::string *display_names = tags_display_names_[workshop_id];
      v8::Local<v8::Array> v8_tags = Nan::New<v8::Array>(num_tags);
      v8::Local<v8::Array> v8_tags_display_names = Nan::New<v8::Array>(num_tags);
      for (size_t j = 0; j < num_tags; j++) {
        std::string tag = tags[j];
        Nan::Set(v8_tags, j, Nan::New(tag).ToLocalChecked());
        std::string key = display_names[j];
        Nan::Set(v8_tags_display_names, j, Nan::New(key).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("tags").ToLocalChecked(), v8_tags);
      Nan::Set(item, Nan::New("tagsDisplayNames").ToLocalChecked(), v8_tags_display_names);
      delete[] tags;
      delete[] display_names;
    }
    // Write out additional previews
    if (additional_preview_types_.size() > 0 && additional_preview_types_.count(workshop_id) > 0) {
      std::string *urls = additional_preview_urls_[workshop_id];
      EItemPreviewType *types = additional_preview_types_[workshop_id];
      int numChildren = sizeof(urls) / sizeof(std::string);
      v8::Local<v8::Array> v8_urls = Nan::New<v8::Array>(numChildren);
      v8::Local<v8::Array> v8_types = Nan::New<v8::Array>(numChildren);
      for (size_t j = 0; j < numChildren; j++) {
        EItemPreviewType type = types[j];
        Nan::Set(v8_types, j, Nan::New(PreviewTypeToString(type)).ToLocalChecked());
        std::string url = urls[j];
        Nan::Set(v8_urls, j, Nan::New(url).ToLocalChecked());
      }
      Nan::Set(item, Nan::New("additionalPreviewURLs").ToLocalChecked(), v8_urls);
      Nan::Set(item, Nan::New("additionalPreviewTypes").ToLocalChecked(), v8_types);
      delete[] urls;
      delete[] types;
    }
    bool is_updated =
        std::find(download_ugc_items_handle_.begin(), download_ugc_items_handle_.end(), ugc_items_[i].m_hFile) != download_ugc_items_handle_.end();
    Nan::Set(item, Nan::New("isUpdated").ToLocalChecked(), Nan::New(is_updated));
    Nan::Set(items, i, item);
  }
  child_map_.clear();
  additional_preview_types_.clear();
  additional_preview_urls_.clear();
  tags_.clear();
  num_tags_.clear();
  tags_display_names_.clear();
  metadata_.clear();
  key_value_tags_keys_.clear();
  key_value_tags_values_.clear();

  v8::Local<v8::Value> argv[] = {items};
  Nan::AsyncResource resource("greenworks:SynchronizeItemsWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}

UnsubscribePublishedFileWorker::UnsubscribePublishedFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback,
                                                               PublishedFileId_t unsubscribe_file_id)
    : SteamCallbackAsyncWorker(success_callback, error_callback), unsubscribe_file_id_(unsubscribe_file_id) {}
void UnsubscribePublishedFileWorker::Execute() {
  SteamAPICall_t result = SteamUGC()->UnsubscribeItem(unsubscribe_file_id_);
  call_result_.Set(result, this, &UnsubscribePublishedFileWorker::OnUnsubscribeCompleted);
  // Wait for unsubscribing job completed.
  WaitForCompleted();
}
void UnsubscribePublishedFileWorker::OnUnsubscribeCompleted(RemoteStorageUnsubscribePublishedFileResult_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on downloading file: Steam API IO Failure");
  }
  result_ = result->m_eResult;
  is_completed_ = true;
}
void UnsubscribePublishedFileWorker::HandleOKCallback() {
  Nan::HandleScope scope;
  v8::Local<v8::Value> argv[] = {Nan::New(result_)};
  Nan::AsyncResource resource("greenworks:UnsubscribePublishedFileWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}

SubscribePublishedFileWorker::SubscribePublishedFileWorker(Nan::Callback *success_callback, Nan::Callback *error_callback, PublishedFileId_t Subscribe_file_id)
    : SteamCallbackAsyncWorker(success_callback, error_callback), subscribe_file_id_(Subscribe_file_id) {}
void SubscribePublishedFileWorker::Execute() {
  SteamAPICall_t result = SteamUGC()->SubscribeItem(subscribe_file_id_);
  call_result_.Set(result, this, &SubscribePublishedFileWorker::OnSubscribeCompleted);
  // Wait for Subscribing job completed.
  WaitForCompleted();
}
void SubscribePublishedFileWorker::OnSubscribeCompleted(RemoteStorageSubscribePublishedFileResult_t *result, bool io_failure) {
  if (io_failure) {
    SetErrorMessage("Error on downloading file: Steam API IO Failure");
  }
  result_ = result->m_eResult;
  is_completed_ = true;
}
void SubscribePublishedFileWorker::HandleOKCallback() {
  Nan::HandleScope scope;
  v8::Local<v8::Value> argv[] = {Nan::New(result_)};
  Nan::AsyncResource resource("greenworks:SubscribePublishedFileWorker.HandleOKCallback");
  callback->Call(1, argv, &resource);
}
} // namespace greenworks

// namespace greenworks
