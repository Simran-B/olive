/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "effectloaders.h"

#include "effects/effect.h"
#include "effects/transition.h"
#include "global/path.h"
#include "panels/panels.h"
#include "panels/effectcontrols.h"
#include "global/crossplatformlib.h"
#include "global/config.h"

#include <QDir>
#include <QXmlStreamReader>

#include <QDebug>

#ifndef NOFREI0R
#include <frei0r.h>
typedef void (*f0rGetPluginInfo)(f0r_plugin_info_t* info);
#endif

QMutex olive::effects_loaded;

void load_internal_effects() {
  if (!olive::CurrentRuntimeConfig.shaders_are_enabled) qWarning() << "Shaders are disabled, some effects may be nonfunctional";

  EffectMeta em;

  // load internal effects
  em.path = ":/internalshaders";

  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Volume";
  em.internal = EFFECT_INTERNAL_VOLUME;
  olive::effects.append(em);

  em.name = "Pan";
  em.internal = EFFECT_INTERNAL_PAN;
  olive::effects.append(em);

#ifndef NOVST
  em.name = "VST Plugin 2.x";
  em.internal = EFFECT_INTERNAL_VST;
  olive::effects.append(em);
#endif

  em.name = "Tone";
  em.internal = EFFECT_INTERNAL_TONE;
  olive::effects.append(em);

  em.name = "Noise";
  em.internal = EFFECT_INTERNAL_NOISE;
  olive::effects.append(em);

  em.name = "Fill Left/Right";
  em.internal = EFFECT_INTERNAL_FILLLEFTRIGHT;
  olive::effects.append(em);

  em.subtype = EFFECT_TYPE_VIDEO;

  em.name = "Transform";
  em.category = "Distort";
  em.internal = EFFECT_INTERNAL_TRANSFORM;
  olive::effects.append(em);

  em.name = "Corner Pin";
  em.internal = EFFECT_INTERNAL_CORNERPIN;
  olive::effects.append(em);

  /*em.name = "Mask";
  em.internal = EFFECT_INTERNAL_MASK;
  olive::effects.append(em);*/

  em.name = "Shake";
  em.internal = EFFECT_INTERNAL_SHAKE;
  olive::effects.append(em);

  em.name = "Text";
  em.category = "Render";
  em.internal = EFFECT_INTERNAL_TEXT;
  olive::effects.append(em);

  em.name = "Rich Text";
  em.category = "Render";
  em.internal = EFFECT_INTERNAL_RICHTEXT;
  olive::effects.append(em);

  em.name = "Timecode";
  em.internal = EFFECT_INTERNAL_TIMECODE;
  olive::effects.append(em);

  em.name = "Solid";
  em.internal = EFFECT_INTERNAL_SOLID;
  olive::effects.append(em);

  // internal transitions
  em.type = EFFECT_TYPE_TRANSITION;
  em.category = "";

  em.name = "Cross Dissolve";
  em.internal = TRANSITION_INTERNAL_CROSSDISSOLVE;
  olive::effects.append(em);

  em.subtype = EFFECT_TYPE_AUDIO;

  em.name = "Linear Fade";
  em.internal = TRANSITION_INTERNAL_LINEARFADE;
  olive::effects.append(em);

  em.name = "Exponential Fade";
  em.internal = TRANSITION_INTERNAL_EXPONENTIALFADE;
  olive::effects.append(em);

  em.name = "Logarithmic Fade";
  em.internal = TRANSITION_INTERNAL_LOGARITHMICFADE;
  olive::effects.append(em);
}

void load_shader_effects() {
  QList<QString> effects_paths = get_effects_paths();

  for (int h=0;h<effects_paths.size();h++) {
    const QString& effects_path = effects_paths.at(h);
    QDir effects_dir(effects_path);
    if (effects_dir.exists()) {
      QList<QString> entries = effects_dir.entryList(QStringList("*.xml"), QDir::Files);
      for (int i=0;i<entries.size();i++) {
        QFile file(effects_path + "/" + entries.at(i));
        if (!file.open(QIODevice::ReadOnly)) {
          qCritical() << "Could not open" << entries.at(i);
          return;
        }

        QXmlStreamReader reader(&file);
        while (!reader.atEnd()) {
          if (reader.name() == "effect") {
            QString effect_name = "";
            QString effect_cat = "";
            const QXmlStreamAttributes attr = reader.attributes();
            for (int j=0;j<attr.size();j++) {
              if (attr.at(j).name() == "name") {
                effect_name = attr.at(j).value().toString();
              } else if (attr.at(j).name() == "category") {
                effect_cat = attr.at(j).value().toString();
              }
            }
            if (!effect_name.isEmpty()) {
              EffectMeta em;
              em.type = EFFECT_TYPE_EFFECT;
              em.subtype = EFFECT_TYPE_VIDEO;
              em.name = effect_name;
              em.category = effect_cat;
              em.filename = file.fileName();
              em.path = effects_path;
              em.internal = -1;
              olive::effects.append(em);
            } else {
              qCritical() << "Invalid effect found in" << entries.at(i);
            }
            break;
          }
          reader.readNext();
        }

        file.close();
      }
    }
  }
}

void EffectInit::StartLoading() {
  EffectInit* init_thread = new EffectInit();
  QObject::connect(init_thread, SIGNAL(finished()), init_thread, SLOT(deleteLater()));
  init_thread->start();
}

#ifndef NOFREI0R
void load_frei0r_effects_worker(const QString& dir, EffectMeta& em, QVector<QString>& loaded_names) {
  QDir search_dir(dir);
  if (search_dir.exists()) {
    QList<QString> entry_list = search_dir.entryList(LibFilter(), QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    for (int j=0;j<entry_list.size();j++) {
      QString entry_path = search_dir.filePath(entry_list.at(j));
      if (QFileInfo(entry_path).isDir()) {
        load_frei0r_effects_worker(entry_path, em, loaded_names);
      } else {
        ModulePtr effect = LibLoad(entry_path);
        if (effect != nullptr) {
          f0rGetPluginInfo get_info_func = reinterpret_cast<f0rGetPluginInfo>(LibAddress(effect, "f0r_get_plugin_info"));
          if (get_info_func != nullptr) {
            f0r_plugin_info_t info;
            get_info_func(&info);

            if (!loaded_names.contains(info.name)
                && info.plugin_type == F0R_PLUGIN_TYPE_FILTER
                && info.color_model == F0R_COLOR_MODEL_RGBA8888) {
              em.name = info.name;
              em.path = dir;
              em.filename = entry_list.at(j);
              em.tooltip = QString("%1\n%2\n%3\n%4").arg(em.name, info.author, info.explanation, em.filename);

              loaded_names.append(em.name);

              olive::effects.append(em);
            }
//                        qDebug() << "Found:" << info.name << "by" << info.author;
          }
          LibClose(effect);
        }
//                qDebug() << search_dir.filePath(entry_list.at(j));
      }
    }
  }
}

void load_frei0r_effects() {
  QList<QString> effect_dirs = get_effects_paths();

  // add defined paths for frei0r plugins on unix
#if defined(__APPLE__) || defined(__linux__) || defined(__HAIKU__)
  effect_dirs.prepend("/usr/lib/frei0r-1");
  effect_dirs.prepend("/usr/local/lib/frei0r-1");
  effect_dirs.prepend(QDir::homePath() + "/.frei0r-1/lib");
#endif

  QString env_path(qgetenv("FREI0R_PATH"));
  if (!env_path.isEmpty()) effect_dirs.append(env_path);

  QVector<QString> loaded_names;

  // search for paths
  EffectMeta em;
  em.category = "Frei0r";
  em.type = EFFECT_TYPE_EFFECT;
  em.subtype = EFFECT_TYPE_VIDEO;
  em.internal = EFFECT_INTERNAL_FREI0R;

  for (int i=0;i<effect_dirs.size();i++) {
    load_frei0r_effects_worker(effect_dirs.at(i), em, loaded_names);
  }
}
#endif

EffectInit::EffectInit() {
  olive::effects_loaded.lock();
}

void EffectInit::run() {
  qInfo() << "Initializing effects...";
  load_internal_effects();
  load_shader_effects();
#ifndef NOFREI0R
  load_frei0r_effects();
#endif
  olive::effects_loaded.unlock();
  qInfo() << "Finished initializing effects";
}