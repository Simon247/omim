package com.mapswithme.maps.editor;

import android.support.annotation.IdRes;
import android.support.annotation.IntRange;
import android.support.v4.app.Fragment;
import android.support.v7.widget.RecyclerView;
import android.support.v7.widget.SwitchCompat;
import android.util.SparseArray;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.List;

import com.mapswithme.maps.R;
import com.mapswithme.maps.editor.data.HoursMinutes;
import com.mapswithme.maps.editor.data.TimeFormatUtils;
import com.mapswithme.maps.editor.data.Timespan;
import com.mapswithme.maps.editor.data.Timetable;
import com.mapswithme.util.UiUtils;

public class SimpleTimetableAdapter extends RecyclerView.Adapter<SimpleTimetableAdapter.BaseTimetableViewHolder>
                                 implements HoursMinutesPickerFragment.OnPickListener,
                                            TimetableFragment.TimetableProvider
{
  private static final int TYPE_TIMETABLE = 0;
  private static final int TYPE_ADD_TIMETABLE = 1;

  private static final int ID_OPENING = 0;
  private static final int ID_CLOSING = 1;

  private static final int[] DAYS = {R.id.day1, R.id.day2, R.id.day3, R.id.day4, R.id.day5, R.id.day6, R.id.day7};

  private final Fragment mFragment;

  private List<Timetable> mItems = new ArrayList<>();
  private Timetable mComplementItem;
  private int mPickingPosition;

  SimpleTimetableAdapter(Fragment fragment)
  {
    mFragment = fragment;
    mItems = new ArrayList<>(Arrays.asList(OpeningHours.nativeGetDefaultTimetables()));
    refreshComplement();
  }

  public void setTimetables(Timetable[] tts)
  {
    mItems = new ArrayList<>(Arrays.asList(tts));
    refreshComplement();
    notifyDataSetChanged();
  }

  @Override
  public String getTimetables()
  {
    return OpeningHours.nativeTimetablesToString(mItems.toArray(new Timetable[mItems.size()]));
  }

  @Override
  public BaseTimetableViewHolder onCreateViewHolder(ViewGroup parent, int viewType)
  {
    final LayoutInflater inflater = LayoutInflater.from(parent.getContext());
    return viewType == TYPE_TIMETABLE ? new TimetableViewHolder(inflater.inflate(R.layout.item_timetable, parent, false))
                                      : new AddTimetableViewHolder(inflater.inflate(R.layout.item_timetable_add, parent, false));
  }

  @Override
  public void onBindViewHolder(BaseTimetableViewHolder holder, int position)
  {
    holder.onBind();
  }

  @Override
  public int getItemCount()
  {
    return mItems.size() + 1;
  }

  @Override
  public int getItemViewType(int position)
  {
    return position == getItemCount() - 1 ? TYPE_ADD_TIMETABLE
                                          : TYPE_TIMETABLE;
  }

  protected void addTimetable()
  {
    mItems.add(OpeningHours.nativeGetComplementTimetable(mItems.toArray(new Timetable[mItems.size()])));
    notifyItemInserted(mItems.size() - 1);
    refreshComplement();
  }

  protected void removeTimetable(int position)
  {
    mItems.remove(position);
    notifyItemRemoved(position);
    refreshComplement();
  }

  protected void refreshComplement()
  {
    mComplementItem = OpeningHours.nativeGetComplementTimetable(mItems.toArray(new Timetable[mItems.size()]));
    notifyItemChanged(getItemCount() - 1);
  }

  protected void pickTime(int position, @IntRange(from = HoursMinutesPickerFragment.TAB_FROM, to = HoursMinutesPickerFragment.TAB_TO) int tab,
                          @IntRange(from = ID_OPENING, to = ID_CLOSING) int id)
  {
    final Timetable data = mItems.get(position);
    mPickingPosition = position;
    HoursMinutesPickerFragment.pick(mFragment.getActivity(), mFragment.getChildFragmentManager(),
                                    data.workingTimespan.start, data.workingTimespan.end,
                                    tab, id);
  }

  @Override
  public void onHoursMinutesPicked(HoursMinutes from, HoursMinutes to, int id)
  {
    final Timetable item = mItems.get(mPickingPosition);
    if (id == ID_OPENING)
      mItems.set(mPickingPosition, OpeningHours.nativeSetOpeningTime(item, new Timespan(from, to)));
    else
      mItems.set(mPickingPosition, OpeningHours.nativeAddClosedSpan(item, new Timespan(from, to)));
    notifyItemChanged(mPickingPosition);
  }

  private void removeClosedHours(int position, int closedPosition)
  {
    mItems.set(position, OpeningHours.nativeRemoveClosedSpan(mItems.get(position), closedPosition));
    notifyItemChanged(position);
  }

  protected void addWorkingDay(int day, int position)
  {
    final Timetable tts[] = mItems.toArray(new Timetable[mItems.size()]);
    mItems = new ArrayList<>(Arrays.asList(OpeningHours.nativeAddWorkingDay(tts, position, day)));
    refreshComplement();
    notifyDataSetChanged();
  }

  protected void removeWorkingDay(int day, int position)
  {
    final Timetable tts[] = mItems.toArray(new Timetable[mItems.size()]);
    mItems = new ArrayList<>(Arrays.asList(OpeningHours.nativeRemoveWorkingDay(tts, position, day)));
    refreshComplement();
    notifyDataSetChanged();
  }

  private void setFullday(int position, boolean fullday)
  {
    mItems.set(position, OpeningHours.nativeSetIsFullday(mItems.get(position), fullday));
    notifyItemChanged(position);
  }

  public abstract class BaseTimetableViewHolder extends RecyclerView.ViewHolder
  {
    public BaseTimetableViewHolder(View itemView)
    {
      super(itemView);
    }

    abstract void onBind();
  }

  public class TimetableViewHolder extends BaseTimetableViewHolder implements View.OnClickListener, CompoundButton.OnCheckedChangeListener
  {
    // Limit closed spans to avoid dynamic inflation of views in recycler's children. Yeah, its a hack.
    static final int MAX_CLOSED_SPANS = 10;

    SparseArray<CheckBox> days = new SparseArray<>(7);
    View allday;
    SwitchCompat swAllday;
    View schedule;
    View openClose;
    View open;
    View close;
    TextView tvOpen;
    TextView tvClose;
    View closedHours[] = new View[MAX_CLOSED_SPANS];
    View addClosed;
    View deleteTimetable;

    public TimetableViewHolder(View itemView)
    {
      super(itemView);
      initDays();

      allday = itemView.findViewById(R.id.allday);
      allday.setOnClickListener(this);
      swAllday = (SwitchCompat) allday.findViewById(R.id.sw__allday);
      schedule = itemView.findViewById(R.id.schedule);
      openClose = schedule.findViewById(R.id.time_open_close);
      open = openClose.findViewById(R.id.time_open);
      open.setOnClickListener(this);
      close = openClose.findViewById(R.id.time_close);
      close.setOnClickListener(this);
      tvOpen = (TextView) open.findViewById(R.id.tv__time_open);
      tvClose = (TextView) close.findViewById(R.id.tv__time_close);
      addClosed = schedule.findViewById(R.id.tv__add_closed);
      addClosed.setOnClickListener(this);
      deleteTimetable = itemView.findViewById(R.id.tv__remove_timetable);
      deleteTimetable.setOnClickListener(this);

      final ViewGroup closedHost = (ViewGroup) itemView.findViewById(R.id.closed_host);
      for (int i = 0; i < MAX_CLOSED_SPANS; i++)
      {
        final View span = LayoutInflater.from(itemView.getContext()).inflate(R.layout.item_timetable_closed_hours, closedHost, false);
        closedHost.addView(span, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, UiUtils.dimen(R.dimen.editor_height_closed)));
        closedHours[i] = span;
        final int finalI = i;
        span.findViewById(R.id.iv__remove_closed).setOnClickListener(new View.OnClickListener()
        {
          @Override
          public void onClick(View v)
          {
            removeClosedHours(getAdapterPosition(), finalI);
          }
        });
      }
    }

    private void initDays()
    {
      final int firstDay = Calendar.getInstance().getFirstDayOfWeek();

      int day = 0;
      for (int i = firstDay; i <= 7; i++)
        addDay(i, DAYS[day++]);
      for (int i = 1; i < firstDay; i++)
        addDay(i, DAYS[day++]);
    }

    @Override
    void onBind()
    {
      final int position = getAdapterPosition();
      final Timetable data = mItems.get(position);
      UiUtils.showIf(position > 0, deleteTimetable);
      tvOpen.setText(data.workingTimespan.start.toString());
      tvClose.setText(data.workingTimespan.end.toString());
      showDays(data.weekdays);
      showSchedule(!data.isFullday);
      showClosedHours(data.closedTimespans);
    }

    @Override
    public void onClick(View v)
    {
      switch (v.getId())
      {
      case R.id.time_open:
        pickTime(getAdapterPosition(), HoursMinutesPickerFragment.TAB_FROM, ID_OPENING);
        break;
      case R.id.time_close:
        pickTime(getAdapterPosition(), HoursMinutesPickerFragment.TAB_TO, ID_OPENING);
        break;
      case R.id.tv__remove_timetable:
        removeTimetable(getAdapterPosition());
        break;
      case R.id.tv__add_closed:
        pickTime(getAdapterPosition(), HoursMinutesPickerFragment.TAB_FROM, ID_CLOSING);
        break;
      case R.id.allday:
        swAllday.toggle();
        break;
      }
    }

    @Override
    public void onCheckedChanged(CompoundButton buttonView, boolean isChecked)
    {
      switch (buttonView.getId())
      {
      case R.id.sw__allday:
        setFullday(getAdapterPosition(), isChecked);
        break;
      case R.id.chb__day:
        final int dayIndex = (Integer) buttonView.getTag();
        switchWorkingDay(dayIndex);
        break;
      }
    }

    void showDays(@IntRange(from = 1, to = 7) int[] weekdays)
    {
      for (int i = 1; i <= 7; i++)
        checkWithoutCallback(days.get(i), false);

      for (int checked : weekdays)
        checkWithoutCallback(days.get(checked), true);
    }

    void showSchedule(boolean show)
    {
      UiUtils.showIf(show, schedule);
      checkWithoutCallback(swAllday, !show);
    }

    private void showClosedHours(Timespan[] closedSpans)
    {
      int i = 0;
      for (Timespan timespan : closedSpans)
      {
        if (i == MAX_CLOSED_SPANS)
          return;

        if (timespan == null)
          UiUtils.hide(closedHours[i]);
        else
        {
          UiUtils.show(closedHours[i]);
          ((TextView) closedHours[i].findViewById(R.id.tv__closed)).setText(timespan.toString());
        }

        i++;
      }

      while (i < MAX_CLOSED_SPANS)
        UiUtils.hide(closedHours[i++]);
    }

    /**
     * @param dayIndex 1 based index of a day in the week
     * @param id       resource id of day view
     */
    private void addDay(@IntRange(from = 1, to = 7) final int dayIndex, @IdRes int id)
    {
      final View day = itemView.findViewById(id);
      final CheckBox checkBox = (CheckBox) day.findViewById(R.id.chb__day);
      // Save index of the day to get it back when checkbox will be toggled.
      checkBox.setTag(dayIndex);
      days.put(dayIndex, checkBox);
      day.setOnClickListener(new View.OnClickListener()
      {
        @Override
        public void onClick(View v)
        {
          checkBox.toggle();
        }
      });

      ((TextView) day.findViewById(R.id.tv__day)).setText(TimeFormatUtils.formatShortWeekday(dayIndex));
    }

    private void switchWorkingDay(@IntRange(from = 1, to = 7) int dayIndex)
    {
      final CheckBox checkBox = days.get(dayIndex);
      if (checkBox.isChecked())
        addWorkingDay(dayIndex, getAdapterPosition());
      else
        removeWorkingDay(dayIndex, getAdapterPosition());
    }

    private void checkWithoutCallback(CompoundButton button, boolean check)
    {
      button.setOnCheckedChangeListener(null);
      button.setChecked(check);
      button.setOnCheckedChangeListener(this);
    }
  }

  private class AddTimetableViewHolder extends BaseTimetableViewHolder
  {
    private TextView add;

    public AddTimetableViewHolder(View itemView)
    {
      super(itemView);
      add = (TextView) itemView.findViewById(R.id.btn__add_time);
      add.setOnClickListener(new View.OnClickListener()
      {
        @Override
        public void onClick(View v)
        {
          addTimetable();
        }
      });
    }

    @Override
    void onBind()
    {
      final boolean enable = mComplementItem != null && mComplementItem.weekdays.length != 0;
      final String text = mFragment.getString(R.string.editor_time_add);
      add.setEnabled(enable);
      add.setText(enable ? text + " (" + TimeFormatUtils.formatWeekdays(mComplementItem) + ")"
                         : text);
    }
  }
}
