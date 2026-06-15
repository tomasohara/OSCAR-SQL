#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fix spacing and newlines in existing unfinished translations.

For type="unfinished" strings where the source has leading/trailing spaces or
embedded newlines, adjusts the existing translation to match — without
re-translating. English and French are skipped.
"""
import xml.etree.ElementTree as ET
import os
import glob

TRANSLATIONS_DIR = r'C:\OSCAR\OSCAR-code\Translations'
SKIP_FILES = {'English.en_UK.ts', 'Francais.fr.ts'}


def fix_file(ts_file):
    filename = os.path.basename(ts_file)
    tree = ET.parse(ts_file)
    root = tree.getroot()

    fixed = 0

    for message in root.findall('.//message'):
        if message.get('numerus') == 'yes':
            continue

        source_elem = message.find('source')
        translation_elem = message.find('translation')

        if source_elem is None or translation_elem is None:
            continue
        if translation_elem.get('type') != 'unfinished':
            continue

        source = source_elem.text or ''
        translation = translation_elem.text or ''

        if not source or not translation:
            continue

        has_leading_space  = source != source.lstrip(' ')
        has_trailing_space = source != source.rstrip(' ')
        has_newline        = '\n' in source

        if not (has_leading_space or has_trailing_space or has_newline):
            continue

        new_translation = translation

        # Mirror source leading spaces onto translation
        if has_leading_space:
            n = len(source) - len(source.lstrip(' '))
            if len(new_translation) - len(new_translation.lstrip(' ')) != n:
                new_translation = ' ' * n + new_translation.lstrip(' ')

        # Mirror source trailing spaces onto translation
        if has_trailing_space:
            n = len(source) - len(source.rstrip(' '))
            if len(new_translation) - len(new_translation.rstrip(' ')) != n:
                new_translation = new_translation.rstrip(' ') + ' ' * n

        # Replace literal \n (two chars) with actual newline
        if has_newline and '\\n' in new_translation:
            new_translation = new_translation.replace('\\n', '\n')

        if new_translation != translation:
            translation_elem.text = new_translation
            fixed += 1

    if fixed > 0:
        print(f"{filename}: fixed {fixed}")
        tree.write(ts_file, encoding='utf-8', xml_declaration=True)
    else:
        print(f"{filename}: nothing to fix")

    return fixed


def main():
    ts_files = sorted(glob.glob(os.path.join(TRANSLATIONS_DIR, '*.ts')))
    total = 0
    for ts_file in ts_files:
        if os.path.basename(ts_file) in SKIP_FILES:
            print(f"Skipping {os.path.basename(ts_file)}")
            continue
        total += fix_file(ts_file)
    print(f"\nTotal: {total} fixes")


if __name__ == '__main__':
    main()
