#include "qt/editor_dialog.hpp"

#include "indexer/editable_map_object.hpp"

#include "base/string_utils.hpp"
#include "base/stl_add.hpp"

#include "std/algorithm.hpp"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>

#include <QtCore/QSignalMapper>

constexpr char const * kStreetObjectName = "addr:street";
constexpr char const * kHouseNumberObjectName = "addr:housenumber";
constexpr char const * kPostcodeObjectName = "addr:postcode";
constexpr char const * kInternetObjectName = "internet_access";

EditorDialog::EditorDialog(QWidget * parent, osm::EditableMapObject & emo)
  : QDialog(parent), m_feature(emo)
{
  QGridLayout * grid = new QGridLayout();
  int row = 0;

  {  // Coordinates.
    ms::LatLon const ll = emo.GetLatLon();
    grid->addWidget(new QLabel("Latitude/Longitude:"), row, 0);
    QHBoxLayout * coords = new QHBoxLayout();
    coords->addWidget(new QLabel(QString::fromStdString(strings::to_string_dac(ll.lat, 7) + " " +
                                                        strings::to_string_dac(ll.lon, 7))));
    grid->addLayout(coords, row++, 1);
  }

  {  // Feature types.
    grid->addWidget(new QLabel("Type:"), row, 0);
    string localized = m_feature.GetLocalizedType();
    string const raw = DebugPrint(m_feature.GetTypes());
    if (!strings::EqualNoCase(localized, raw))
      localized += " (" + raw + ")";
    QLabel * label = new QLabel(QString::fromStdString(localized));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(label, row++, 1);
  }

  if (emo.IsNameEditable())
  {  // Names.
    char const * defaultLangStr =
        StringUtf8Multilang::GetLangByCode(StringUtf8Multilang::kDefaultCode);
    // Default name editor is always displayed, even if feature name is empty.
    grid->addWidget(new QLabel(QString("Name:")), row, 0);
    QLineEdit * defaultName = new QLineEdit();
    defaultName->setObjectName(defaultLangStr);
    QGridLayout * namesGrid = new QGridLayout();
    int namesRow = 0;
    namesGrid->addWidget(defaultName, namesRow++, 0, 1, 0);

    for (osm::LocalizedName const & ln : emo.GetLocalizedNames())
    {
      if (ln.m_code == StringUtf8Multilang::kDefaultCode)
      {
        defaultName->setText(QString::fromStdString(ln.m_name));
      }
      else
      {
        char const * langStr = StringUtf8Multilang::GetLangByCode(ln.m_code);
        namesGrid->addWidget(new QLabel(ln.m_lang), namesRow, 0);
        QLineEdit * lineEditName = new QLineEdit(QString::fromStdString(ln.m_name));
        lineEditName->setReadOnly(!emo.IsNameEditable());
        lineEditName->setObjectName(langStr);
        namesGrid->addWidget(lineEditName, namesRow++, 1);
      }
    }
    grid->addLayout(namesGrid, row++, 1);
  }

  if (emo.IsAddressEditable())
  {  // Address rows.
    vector<string> nearbyStreets = emo.GetNearbyStreets();
    // If feature does not have a specified street, display empty combo box.
    if (emo.GetStreet().empty())
      nearbyStreets.insert(nearbyStreets.begin(), "");
    grid->addWidget(new QLabel(kStreetObjectName), row, 0);
    QComboBox * cmb = new QComboBox();
    for (int i = 0; i < nearbyStreets.size(); ++i)
    {
      cmb->addItem(nearbyStreets[i].c_str());
      if (emo.GetStreet() == nearbyStreets[i])
        cmb->setCurrentIndex(i);
    }
    cmb->setObjectName(kStreetObjectName);
    grid->addWidget(cmb, row++, 1);

    grid->addWidget(new QLabel(kHouseNumberObjectName), row, 0);
    QLineEdit * houseLineEdit = new QLineEdit(emo.GetHouseNumber().c_str());
    houseLineEdit->setObjectName(kHouseNumberObjectName);
    grid->addWidget(houseLineEdit, row++, 1);

    grid->addWidget(new QLabel(kPostcodeObjectName), row, 0);
    QLineEdit * postcodeEdit = new QLineEdit(QString::fromStdString(emo.GetPostcode()));
    postcodeEdit->setObjectName(kPostcodeObjectName);
    grid->addWidget(postcodeEdit, row++, 1);
  }

  // Editable metadata rows.
  for (osm::Props const prop : emo.GetEditableProperties())
  {
    string v;
    switch (prop)
    {
    case osm::Props::Phone: v = emo.GetPhone(); break;
    case osm::Props::Fax: v = emo.GetFax(); break;
    case osm::Props::Email: v = emo.GetEmail(); break;
    case osm::Props::Website: v = emo.GetWebsite(); break;
    case osm::Props::Internet:
      {
        grid->addWidget(new QLabel(kInternetObjectName), row, 0);
        QComboBox * cmb = new QComboBox();
        string const values[] = {DebugPrint(osm::Internet::Unknown), DebugPrint(osm::Internet::Wlan),
                                 DebugPrint(osm::Internet::Wired), DebugPrint(osm::Internet::Yes),
                                 DebugPrint(osm::Internet::No)};
        for (auto const & v : values)
          cmb->addItem(v.c_str());
        cmb->setCurrentText(DebugPrint(emo.GetInternet()).c_str());
        cmb->setObjectName(kInternetObjectName);
        grid->addWidget(cmb, row++, 1);
      }
      continue;
    case osm::Props::Cuisine: v = strings::JoinStrings(emo.GetLocalizedCuisines(), ", "); break;
    case osm::Props::OpeningHours: v = emo.GetOpeningHours(); break;
    case osm::Props::Stars: v = strings::to_string(emo.GetStars()); break;
    case osm::Props::Operator: v = emo.GetOperator(); break;
    case osm::Props::Elevation:
      {
        double ele;
        if (emo.GetElevation(ele))
          v = strings::to_string_dac(ele, 2);
      }
      break;
    case osm::Props::Wikipedia: v = emo.GetWikipedia(); break;
    case osm::Props::Flats: v = emo.GetFlats(); break;
    case osm::Props::BuildingLevels: v = emo.GetBuildingLevels(); break;
    }
    QString const fieldName = QString::fromStdString(DebugPrint(prop));
    grid->addWidget(new QLabel(fieldName), row, 0);
    QLineEdit * lineEdit = new QLineEdit(QString::fromStdString(v));
    // Mark line editor to query it's text value when editing is finished.
    lineEdit->setObjectName(fieldName);
    grid->addWidget(lineEdit, row++, 1);
  }

  {  // Dialog buttons.
    QDialogButtonBox * buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(OnSave()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
    // Delete button should send custom int return value from dialog.
    QPushButton * deletePOIButton = new QPushButton("Delete POI");
    QSignalMapper * signalMapper = new QSignalMapper();
    connect(deletePOIButton, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(deletePOIButton, QDialogButtonBox::DestructiveRole);
    connect(signalMapper, SIGNAL(mapped(int)), this, SLOT(done(int)));
    buttonBox->addButton(deletePOIButton, QDialogButtonBox::DestructiveRole);
    grid->addWidget(buttonBox, row++, 1);
  }

  setLayout(grid);
  setWindowTitle("OSM Editor");
}

void EditorDialog::OnSave()
{
  // Store all edits.
  if (m_feature.IsNameEditable())
  {
    StringUtf8Multilang names;
    for (int8_t langCode = StringUtf8Multilang::kDefaultCode;
         langCode < StringUtf8Multilang::kMaxSupportedLanguages; ++langCode)
    {
      QLineEdit * le = findChild<QLineEdit *>(StringUtf8Multilang::GetLangByCode(langCode));
      if (!le)
        continue;
      string const name = le->text().toStdString();
      if (!name.empty())
        names.AddString(langCode, name);
    }
    m_feature.SetName(names);
  }

  if (m_feature.IsAddressEditable())
  {
    m_feature.SetHouseNumber(findChild<QLineEdit *>(kHouseNumberObjectName)->text().toStdString());
    m_feature.SetStreet(findChild<QComboBox *>(kStreetObjectName)->currentText().toStdString());
    m_feature.SetPostcode(findChild<QLineEdit *>(kPostcodeObjectName)->text().toStdString());
  }

  for (osm::Props const prop : m_feature.GetEditableProperties())
  {
    if (prop == osm::Props::Internet)
    {
      QComboBox * cmb = findChild<QComboBox *>(kInternetObjectName);
      string const str = cmb->currentText().toStdString();
      osm::Internet v = osm::Internet::Unknown;
      if (str == DebugPrint(osm::Internet::Wlan))
        v = osm::Internet::Wlan;
      else if (str == DebugPrint(osm::Internet::Wired))
        v = osm::Internet::Wired;
      else if (str == DebugPrint(osm::Internet::No))
        v = osm::Internet::No;
      else if (str == DebugPrint(osm::Internet::Yes))
        v = osm::Internet::Yes;
      m_feature.SetInternet(v);
      continue;
    }

    QLineEdit * editor = findChild<QLineEdit *>(QString::fromStdString(DebugPrint(prop)));
    if (!editor)
      continue;

    string const v = editor->text().toStdString();
    switch (prop)
    {
    case osm::Props::Phone: m_feature.SetPhone(v); break;
    case osm::Props::Fax: m_feature.SetFax(v); break;
    case osm::Props::Email: m_feature.SetEmail(v); break;
    case osm::Props::Website: m_feature.SetWebsite(v); break;
    case osm::Props::Internet: ASSERT(false, ("Is handled separately above."));
    case osm::Props::Cuisine:
    {
      vector<string> cuisines;
      strings::Tokenize(v, ";", MakeBackInsertFunctor(cuisines));
      m_feature.SetCuisines(cuisines);
    }
    break;
    case osm::Props::OpeningHours: m_feature.SetOpeningHours(v); break;
    case osm::Props::Stars:
    {
      int num;
      if (strings::to_int(v, num))
        m_feature.SetStars(num);
    }
    break;
    case osm::Props::Operator: m_feature.SetOperator(v); break;
    case osm::Props::Elevation:
    {
      double ele;
      if (strings::to_double(v, ele))
        m_feature.SetElevation(ele);
    }
    break;
    case osm::Props::Wikipedia: m_feature.SetWikipedia(v); break;
    case osm::Props::Flats: m_feature.SetFlats(v); break;
    case osm::Props::BuildingLevels: m_feature.SetBuildingLevels(v); break;
    }
  }
  accept();
}
