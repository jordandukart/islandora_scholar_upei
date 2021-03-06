<?php

/**
 * @file
 * 
 */
function scholar_migrate_refworks_table() {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');

  $query_string = 'select $object $title from <#ri> where ($object <fedora-model:hasModel> <info:fedora/islandora:refworksCModel> and $object <dc:title> $title and $object <fedora-model:state> <info:fedora/fedora-system:def/model#Active>) minus $object <fedora-model:hasModel> <info:fedora/islandora:citationCModel>';
  $query_string = htmlentities(urlencode($query_string));
  $content = '';
  $url = variable_get('fedora_repository_url', 'http://localhost:8080/fedora/risearch');
  $url .= "?type=tuples&flush=TRUE&format=Sparql&limit=200&offset=0&lang=itql&stream=on&query=" . $query_string;
  $content .= do_curl($url);
  $query = new SimpleXMLElement($content);
//  $list = '';
  $results = $query->results->result;
  foreach ($results as $result) {
    $pid = explode('/', $result->object['uri']);
    $title = $result->title;
    $objects[$pid[1]] = $title;
    $keys[] = $pid[1];
  }

//  $form['list'] = array(
//    '#value' => $list,
//  );

  if ($keys != NULL) {

    $table = array(
      '#header' => array(theme('table_select_header_cell'), t('PID'), t('Title'), ''),
      '#theme' => THEME_BIBLIOGRAPHY_FORM_TABLE,
      '#tree' => TRUE,
      'rows' => array(),
      'selections' => array(
        '#type' => 'checkboxes',
        '#options' => array_fill_keys($keys, ''),
      ),
    );
    $rows = &$table['rows'];
    foreach ($objects as $key => $object) {
      $rows[] = array(
        '#pid' => $key,
        'pid' => array('#value' => l($key, 'fedora/repository/' . $key)),
        'title' => array('#value' => $object),
      );
    }
//    $table['result'] = TRUE;
  }
  else {
//    $table = array(
//      '#value' => t('No refworks objects found!'),
//    );
    $table = FALSE;
  }
  return $table;
}

function scholar_migrate_refworks_form() {

  $table = scholar_migrate_refworks_table();

  if ($table != FALSE) {

    $form['table'] = array(
      'table' => $table,
    );

    $form['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Submit'),
    );
  }
  else {
    $form['table'] = array(
      '#value' => t('No refworks objects found!'),
    );
  }
  return $form;
}

function scholar_migrate_refworks_form_submit($form, &$form_state) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $pids = array_filter($form_state['values']['table']['selections']);
  $pid_list = implode(', ', $pids);
  drupal_set_message(t("PIDS converted: @pid_list", array("@pid_list" => $pid_list)));
  foreach ($pids as $pid) {
    $object = new Fedora_Item($pid);
    $datastreams = $object->get_datastreams_list_as_array();
    if (!array_key_exists('MODS', $datastreams)) {
      $rxml = $object->get_datastream_dissemination('refworks');
      $input = new DomDocument($rxml);
      module_load_include('inc','scholar', 'Batch');     
      $xml = scholar_batch_ingest_mods_from_refworks($input, $rxml);
      $xml = str_replace(array('&#39;', '&#039;', '&apos;', '&amp;#39;', '&amp;#039;', '&amp;apos;'), "'", $xml);
      $u1 = $input->getElementsByTagName('u1');
      $u1_array = array();
      if ($u1->length > 1) {
        foreach ($u1 as $u) {
          $u1_array[] = $u->nodeValue;
        }
      }
      else {
        $u1_array = explode(';', $u1->item(0)->nodeValue);
      }
      $u2 = $input->getElementsByTagName('u2');
      $u2_array = array();
      if ($u2->length > 1) {
        foreach ($u2 as $u) {
          $u2_array[] = $u->nodeValue;
        }
      }
      else {
        $u2_array = explode(';', $u2->item(0)->nodeValue);
      }
      $object->add_datastream_from_string($xml, 'MODS', 'MODS datastream', 'text/xml', 'X');
      // Check if the relationships for u1 and u2 exist
      // If they don't they create them in the RELS-EXT
      // TODO: create exist_relationship function to use elsewhere
      $rel1 = $object->get_relationships('hasResearcher');
      if ($rel1 == NULL) {
        foreach ($u1_array as $researcher) {
          $object->add_relationship('hasResearcher', str_replace(' ', '_', trim($researcher)));
        }
      }
      else {
        drupal_set_message(t('Relationship hasResearcher already exists!'), 'warning');
      }
      $rel2 = $object->get_relationships('hasDepartment');
      if ($rel2 == NULL) {
        foreach ($u2_array as $department) {
          $object->add_relationship('hasDepartment', str_replace(' ', '_', trim($department)));
        }
      }
      else {
        drupal_set_message(t('Relationship hasDepartment already exists!'), 'warning');
      }
    }
    else {
      drupal_set_message(t('MODS datastream already exists for @pid', array('@pid' => $pid)), 'warning');
    }
    // Ensure that the previous edit has completed before starting
    // the next to prevent errors due to the object being locked
    $model_edit_test = '';
    $model_test = $object->get_rdf_relationships();
    $arr = $model_test['fedora-model:hasModel'];
    $update_model = FALSE;
    $update_collection = FALSE;
    if (!empty($arr)) {
      if (in_array('islandora:refworksCModel', $arr) && !in_array('islandora:citationCModel', $arr)) {
        $update_model = TRUE;
      }
    }
    if ($update_model) {
      //      $model_result = $object->purge_relationship('hasModel', 'islandora:refworksCModel');
      //      if ($model_result) {
      $object->add_relationship('hasModel', 'islandora:citationCModel', FEDORA_MODEL_URI);
      //assuming we are part of the citation collection since we are coming from refworks.
      //$object->add_relationship('fedora:isMemberOfCollection', 'ir:citationCollection');
      $model_edit_test = TRUE;
      //      }
    }
    if ($model_edit_test) {
//      $collection_test = $object->get_relationships('fedora:isMemberOfCollection');
//      if ($collection_test[0]['object'] == 'ir:collection') {
//        $collection_result = $object->purge_relationship('fedora:isMemberOfCollection', 'ir:collection');
//        if ($collection_result) {
      $object->add_relationship('fedora:isMemberOfCollection', 'ir:citationCollection');
//        }
//      }
    }
  }
}

//function islandora_scholar_check_relationship($pid, $relationship) {
//    module_load_include('inc', 'fedora_repository', 'api/fedora_item');
//  $object = new Fedora_Item($pid);
//  $test = $object->get_relationships($relationship);
//  if ($test == NULL) {
//    return FALSE;
//  }
//  else {
//    return TRUE;
//  }
//}