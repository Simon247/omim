package com.mapswithme.maps.downloader;

import android.support.annotation.NonNull;
import android.text.TextUtils;

/**
 * Class representing a single item in countries hierarchy.
 * Fields are filled by native code.
 */
public final class CountryItem implements Comparable<CountryItem>
{
  private static String ROOT;

  // Must correspond to ItemCategory in MapManager.cpp
  static final int CATEGORY_NEAR_ME = 0;
  static final int CATEGORY_DOWNLOADED = 1;
  static final int CATEGORY_AVAILABLE = 2;

  // Must correspond to NodeStatus in storage_defines.hpp
  public static final int STATUS_UNKNOWN = 0;
  public static final int STATUS_PROGRESS = 1;       // Downloading a new mwm or updating an old one.
  public static final int STATUS_ENQUEUED = 2;       // An mwm is waiting for downloading in the queue.
  public static final int STATUS_FAILED = 3;         // An error happened while downloading
  public static final int STATUS_UPDATABLE = 4;      // An update for a downloaded mwm is ready according to counties.txt.
  public static final int STATUS_DONE = 5;           // Downloaded mwm(s) is up to date. No need to update it.
  public static final int STATUS_DOWNLOADABLE = 6;   // An mwm can be downloaded but not downloaded yet.
  public static final int STATUS_PARTLY = 7;         // Leafs of group node has a mix of STATUS_DONE and STATUS_DOWNLOADABLE.

  // Must correspond to NodeErrorCode in storage_defines.hpp
  public static final int ERROR_NONE = 0;
  public static final int ERROR_UNKNOWN = 1;
  public static final int ERROR_OOM = 2;
  public static final int ERROR_NO_INTERNET = 3;

  public final String id;
  public String directParentId;
  public String topmostParentId;

  public String name;
  public String directParentName;
  public String topmostParentName;

  public long size;
  public long totalSize;

  public int childCount;
  public int totalChildCount;

  public int category;
  public int status;
  public int errorCode;
  public boolean present;

  // Percent
  public int progress;

  // Internal ID for grouping under headers in the list
  int headerId;
  // Internal field to store search result name
  String searchResultName;

  public CountryItem(String id)
  {
    this.id = id;
  }

  @Override
  public int hashCode()
  {
    return id.hashCode();
  }

  @SuppressWarnings("SimplifiableIfStatement")
  @Override
  public boolean equals(Object other)
  {
    if (this == other)
      return true;

    if (other == null || getClass() != other.getClass())
      return false;

    return id.equals(((CountryItem)other).id);
  }

  @Override
  public int compareTo(@NonNull CountryItem another)
  {
    int catDiff = (category - another.category);
    if (catDiff != 0)
      return catDiff;

    return name.compareTo(another.name);
  }

  public void update()
  {
    MapManager.nativeGetAttributes(this);

    if (ROOT == null)
      ROOT = MapManager.nativeGetRoot();

    if (TextUtils.equals(ROOT, directParentName))
      directParentName = "";
  }

  public static CountryItem fill(String countryId)
  {
    CountryItem res = new CountryItem(countryId);
    res.update();
    return res;
  }

  public boolean isExpandable()
  {
    return (totalChildCount > 1);
  }

  @Override
  public String toString()
  {
    return "{ id: \"" + id +
           "\", directParentId: \"" + directParentId +
           "\", topmostParentId: \"" + topmostParentId +
           "\", category: \"" + category +
           "\", name: \"" + name +
           "\", directParentName: \"" + directParentName +
           "\", topmostParentName: \"" + topmostParentName +
           "\", present: " + present +
           ", status: " + status +
           ", errorCode: " + errorCode +
           ", headerId: " + headerId +
           ", size: " + size +
           ", totalSize: " + totalSize +
           ", childCount: " + childCount +
           ", totalChildCount: " + totalChildCount +
           ", progress: " + progress +
           "% }";
  }
}
